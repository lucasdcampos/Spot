#pragma once

#include "spot/animation/animation_data.h"
#include "spot/assets/asset_file.h"

namespace Spot::Detail {

void Encode(BinaryWriter& writer, const Skeleton& skeleton);
void Encode(BinaryWriter& writer, const AnimationClip& clip);
std::optional<Skeleton> DecodeSkeleton(std::span<const std::byte> data);
std::optional<AnimationClip> DecodeAnimationClip(std::span<const std::byte> data);

} // namespace Spot::Detail
