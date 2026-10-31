#include "spot/animation/inverse_kinematics.h"

#include "spot/animation/pose.h"
#include "spot/core/math/angles.h"

#include <algorithm>
#include <cmath>

namespace Spot::Pose {

namespace {

constexpr float Epsilon = 1e-6f;

Vector3 GetPosition(const Matrix4& matrix) {
    return {matrix.columns[3].x, matrix.columns[3].y, matrix.columns[3].z};
}

Quaternion GetRotation(const Matrix4& matrix) {
    return Decompose(matrix).rotation;
}

Vector3 Perpendicular(Vector3 v) {
    return Normalize(std::abs(v.x) < 0.9f ? Cross(v, {1, 0, 0}) : Cross(v, {0, 1, 0}));
}

float Angle(Vector3 a, Vector3 b) {
    return std::acos(std::clamp(Dot(Normalize(a), Normalize(b)), -1.0f, 1.0f));
}

// The shortest turn from one direction to another; half a turn about any perpendicular when they are opposite.
Quaternion FromTo(Vector3 from, Vector3 to) {
    from = Normalize(from);
    to = Normalize(to);
    const Vector3 axis = Cross(from, to);
    const float sine = Length(axis);
    const float cosine = Dot(from, to);
    if (sine < Epsilon) {
        return cosine > 0 ? Quaternion{} : Quaternion::FromAxisAngle(Perpendicular(from), Pi);
    }
    return Quaternion::FromAxisAngle(axis / sine, std::atan2(sine, cosine));
}

Quaternion GetParentRotation(const Skeleton& skeleton, std::span<const Matrix4> model, std::size_t bone) {
    const auto parent = skeleton.bones[bone].parent;
    return parent >= 0 ? GetRotation(model[static_cast<std::size_t>(parent)]) : Quaternion{};
}

} // namespace

bool SolveTwoBone(const Skeleton& skeleton, std::size_t end, const TwoBoneGoal& goal,
                  std::span<TransformComponents> local, std::span<Matrix4> model) {
    if (end >= skeleton.bones.size() || local.size() != skeleton.bones.size() || model.size() != local.size() ||
        skeleton.bones[end].parent < 0) {
        return false;
    }
    const auto middle = static_cast<std::size_t>(skeleton.bones[end].parent);
    if (skeleton.bones[middle].parent < 0) {
        return false;
    }
    const auto upper = static_cast<std::size_t>(skeleton.bones[middle].parent);
    const Vector3 a = GetPosition(model[upper]), b = GetPosition(model[middle]), c = GetPosition(model[end]);
    const Vector3 t = goal.position;
    const float upperLength = Length(b - a), lowerLength = Length(c - b);
    if (upperLength < Epsilon || lowerLength < Epsilon) {
        return false;
    }
    const float weight = std::clamp(goal.weight, 0.0f, 1.0f);
    if (weight <= 0) {
        return true;
    }
    // Slightly short of straight, so the bend keeps its side.
    const float reach =
        std::clamp(Length(t - a), std::abs(upperLength - lowerLength) + Epsilon, (upperLength + lowerLength) * 0.9999f);

    // Bends the joints in the plane of the chain until the end is as far from the start as the goal is.
    const Vector3 ac = c - a;
    Vector3 bendAxis = Cross(ac, b - a);
    if (LengthSquared(bendAxis) < Epsilon * Epsilon) {
        bendAxis = Cross(ac, goal.pole);
    }
    bendAxis = LengthSquared(bendAxis) < Epsilon * Epsilon ? Perpendicular(ac) : Normalize(bendAxis);
    const float upperAngle = std::acos(std::clamp(
        (lowerLength * lowerLength - upperLength * upperLength - reach * reach) / (-2.0f * upperLength * reach), -1.0f,
        1.0f));
    const float middleAngle = std::acos(std::clamp(
        (reach * reach - upperLength * upperLength - lowerLength * lowerLength) / (-2.0f * upperLength * lowerLength),
        -1.0f, 1.0f));
    const Quaternion bendUpper = Quaternion::FromAxisAngle(bendAxis, upperAngle - Angle(ac, b - a));
    const Quaternion bendMiddle = Quaternion::FromAxisAngle(bendAxis, middleAngle - Angle(a - b, c - b));
    const Vector3 bentMiddle = bendUpper * (b - a);
    const Vector3 bentEnd = bentMiddle + bendUpper * (bendMiddle * (c - b));

    // Swings the bent chain onto the goal, then twists it about the line to the goal toward the pole.
    const Quaternion swing = FromTo(bentEnd, t - a);
    Quaternion twist;
    const Vector3 line = Normalize(t - a);
    const Vector3 elbow = swing * bentMiddle;
    const Vector3 from = elbow - line * Dot(elbow, line);
    const Vector3 to = goal.pole - line * Dot(goal.pole, line);
    if (LengthSquared(from) > Epsilon && LengthSquared(to) > Epsilon) {
        twist = Quaternion::FromAxisAngle(line, std::atan2(Dot(Cross(from, to), line), Dot(from, to)));
    }

    const Quaternion turn = twist * swing * bendUpper;
    const Quaternion upperRotation = Normalize(turn * GetRotation(model[upper]));
    const Quaternion middleRotation = Normalize(turn * bendMiddle * GetRotation(model[middle]));
    local[upper].rotation = Slerp(
        local[upper].rotation, Normalize(Inverse(GetParentRotation(skeleton, model, upper)) * upperRotation), weight);
    local[middle].rotation = Slerp(local[middle].rotation, Normalize(Inverse(upperRotation) * middleRotation), weight);
    if (goal.rotation) {
        local[end].rotation =
            Slerp(local[end].rotation, Normalize(Inverse(middleRotation) * Normalize(*goal.rotation)), weight);
    }
    ToModel(skeleton, local, model);
    return true;
}

void SolveAim(const Skeleton& skeleton, std::span<const AimBone> chain, Vector3 axis, Vector3 target, float weight,
              float maxAngle, std::span<TransformComponents> local, std::span<Matrix4> model) {
    weight = std::clamp(weight, 0.0f, 1.0f);
    if (chain.empty() || weight <= 0 || local.size() != skeleton.bones.size() || model.size() != local.size() ||
        std::ranges::any_of(chain, [&](const AimBone& link) { return link.bone >= skeleton.bones.size(); })) {
        return;
    }
    const std::size_t tip = chain.back().bone;
    const Vector3 aimed = Normalize(GetRotation(model[tip]) * axis);
    const Vector3 wanted = Normalize(target - GetPosition(model[tip]));
    if (LengthSquared(aimed) < Epsilon || LengthSquared(wanted) < Epsilon) {
        return;
    }
    const Vector3 turnAxis = LengthSquared(Cross(aimed, wanted)) > Epsilon * Epsilon ? Normalize(Cross(aimed, wanted))
                                                                                     : Perpendicular(aimed);
    const float angle = std::min(Angle(aimed, wanted), std::max(maxAngle, 0.0f)) * weight;
    const Vector3 goal = Quaternion::FromAxisAngle(turnAxis, angle) * aimed;
    for (const AimBone& link : chain) {
        const Vector3 current = GetRotation(model[tip]) * axis;
        const Quaternion turn = Slerp(Quaternion{}, FromTo(current, goal), std::clamp(link.weight, 0.0f, 1.0f));
        const Quaternion rotation = Normalize(turn * GetRotation(model[link.bone]));
        local[link.bone].rotation = Normalize(Inverse(GetParentRotation(skeleton, model, link.bone)) * rotation);
        ToModel(skeleton, local, model);
    }
}

} // namespace Spot::Pose
