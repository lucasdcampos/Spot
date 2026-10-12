#pragma once

#include "animation/animation_data.h"

#include <span>

namespace Spot {

// Crossing intervals are (from, to] forward, [to, from) backward. The caller bounds delivery.
std::size_t CollectAnimationEvents(const AnimationClip& clip, float from, float to, bool loop,
                                   std::span<const AnimationEvent*> result, bool includeStart = false);
// Includes whole looping cycles, so wrap-around never moves the character backwards.
TransformComponents GetRootMotionDelta(const Skeleton& skeleton, const AnimationClip& clip, std::size_t bone,
                                       float from, float to, bool loop);

} // namespace Spot
