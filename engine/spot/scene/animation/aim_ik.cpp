#include "spot/scene/animation/aim_ik.h"

#include "spot/animation/inverse_kinematics.h"
#include "spot/core/io/archive.h"
#include "spot/scene/animation/animator.h"
#include "spot/scene/scene.h"

namespace Spot {

void AimIKBone::Serialize(Archive& archive) {
    archive.Field("bone", bone);
    archive.Field("weight", weight, {.min = 0, .max = 1});
}

Animator* AimIK::FindAnimator() const {
    if (m_animator == EntityId::Invalid) {
        return GetEntity().GetComponentInParent<Animator>();
    }
    auto* owner = GetScene().GetEntity(m_animator);
    return owner ? owner->GetComponent<Animator>() : nullptr;
}

std::size_t AimIK::GetFirstBoneIndex(const Animator& animator) const {
    const auto& skeleton = animator.GetSkeleton();
    if (!skeleton) {
        return 0;
    }
    return m_bones.empty() ? skeleton->bones.size()
                           : skeleton->FindBone(m_bones.front().bone).value_or(skeleton->bones.size());
}

bool AimIK::Apply(Animator& animator) const {
    const auto& skeleton = animator.GetSkeleton();
    if (!skeleton || m_bones.empty() || m_weight <= 0) {
        return false;
    }
    std::vector<Pose::AimBone> chain;
    chain.reserve(m_bones.size());
    for (const auto& link : m_bones) {
        const auto bone = skeleton->FindBone(link.bone);
        if (!bone) {
            return false;
        }
        chain.push_back({*bone, link.weight});
    }
    const auto* target = GetScene().GetEntity(m_target);
    const Vector3 world = target ? target->GetTransform().GetWorldPosition() : m_targetPosition;
    const Vector4 point =
        Inverse(animator.GetEntity().GetTransform().GetWorldMatrix()) * Vector4{world.x, world.y, world.z, 1};
    return animator.EditPose([&](const Skeleton& rig, std::span<TransformComponents> local, std::span<Matrix4> model) {
        Pose::SolveAim(rig, chain, m_axis, {point.x, point.y, point.z}, m_weight, m_maxAngle, local, model);
    });
}

void AimIK::OnEnable() {
    if (!GetScene().GetSystem<AnimationSystem>()) {
        GetScene().AddSystem<AnimationSystem>();
    }
}

void AimIK::Serialize(Archive& archive) {
    archive.Field("animator", m_animator, {.componentType = "Animator"});
    archive.Field("bones", m_bones);
    archive.Field("target", m_target);
    archive.Field("targetPosition", m_targetPosition, {.tooltip = "Where it aims, in world space, without a target"});
    archive.Field("axis", m_axis, {.tooltip = "The direction the last bone aims, in its own space"});
    archive.Field("maxAngle", m_maxAngle, {.min = 0, .max = Pi, .tooltip = "Radians it turns at most"});
    archive.Field("weight", m_weight, {.min = 0, .max = 1});
}

} // namespace Spot
