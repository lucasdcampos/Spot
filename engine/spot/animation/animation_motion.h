#pragma once

#include "spot/animation/animation_data.h"

#include <span>

namespace Spot {

// Crossing intervals are (from, to] forward, [to, from) backward. The caller bounds delivery.
std::size_t CollectAnimationEvents(const AnimationClip& clip, float from, float to, bool loop,
                                   std::span<const AnimationEvent*> result, bool includeStart = false);
// How the root bone moves along the ground between the times, and turns about the up axis when the clip's
// rootMotionRotation is on, which root motion gives the entity; its height, tilt and sway stay in the pose (see
// RemoveRootMotion()). Includes whole looping cycles, so wrap-around never moves the character backwards.
TransformComponents GetRootMotionDelta(const Skeleton& skeleton, const AnimationClip& clip, std::size_t bone,
                                       float from, float to, bool loop);
// Takes out of the root bone's pose what GetRootMotionDelta() gives the entity, leaving the rest, such as the hips
// rising and falling as a character walks.
void RemoveRootMotion(const Skeleton& skeleton, std::size_t bone, bool turns, TransformComponents& pose);

} // namespace Spot
