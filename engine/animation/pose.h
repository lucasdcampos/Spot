#pragma once

#include "animation/animation_data.h"

#include <span>

namespace Spot::Pose {

// Caller-owned arrays are sized to bones/joints, so sampling allocates nothing.
void Bind(const Skeleton& skeleton, std::span<TransformComponents> local);
void Sample(const Skeleton& skeleton, const AnimationClip& clip, float time, bool loop,
            std::span<TransformComponents> local);
void Blend(std::span<const TransformComponents> a, std::span<const TransformComponents> b, float weight,
           std::span<TransformComponents> result);
void ToModel(const Skeleton& skeleton, std::span<const TransformComponents> local, std::span<Matrix4> model);
void Palette(const Skeleton& skeleton, std::span<const Matrix4> model, const Matrix4& meshModel,
             std::span<Matrix4> palette);
Vector4 SampleChannel(const AnimationChannel& channel, float time);
float WrapTime(float time, float duration, bool loop);

} // namespace Spot::Pose
