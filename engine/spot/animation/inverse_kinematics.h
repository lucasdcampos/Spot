#pragma once

#include "spot/animation/animation_data.h"

#include <optional>
#include <span>

namespace Spot::Pose {

// Where a chain of two bones, such as an arm or a leg, reaches, in model space.
struct TwoBoneGoal {
    Vector3 position;
    // The end bone's model rotation there; none keeps the rotation it has relative to its parent.
    std::optional<Quaternion> rotation;
    // Where the middle joint, the elbow or the knee, points, in model space; zero keeps the bend of the pose.
    Vector3 pole;
    float weight = 1.0f;
};

// Turns the end bone's parent and grandparent so the end bone reaches the goal, as far as the chain is long. False
// when the bone has no grandparent or the chain has no length. The model pose follows the local one.
bool SolveTwoBone(const Skeleton& skeleton, std::size_t end, const TwoBoneGoal& goal,
                  std::span<TransformComponents> local, std::span<Matrix4> model);

struct AimBone {
    std::size_t bone = 0;
    // The share of the turn still left that this bone takes.
    float weight = 1.0f;
};

// Turns a chain of bones, parents first, so the axis of the last one, in its own space, points at the target, in model
// space: each bone takes its weight of what is left, so a last weight of 1 aims exactly. The turn is at most
// maxAngle radians, and weight blends it in. The model pose follows the local one.
void SolveAim(const Skeleton& skeleton, std::span<const AimBone> chain, Vector3 axis, Vector3 target, float weight,
              float maxAngle, std::span<TransformComponents> local, std::span<Matrix4> model);

} // namespace Spot::Pose
