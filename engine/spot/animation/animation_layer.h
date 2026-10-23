#pragma once

#include "spot/animation/animation_data.h"
#include "spot/assets/asset_ref.h"

#include <span>
#include <string>
#include <vector>

namespace Spot {

class Archive;
enum class AnimationLayerMode { Override, Additive };

struct BoneMaskEntry {
    std::string bone;
    float weight = 1;
    bool descendants = true;
    void Serialize(Archive& archive);
};

struct AnimationLayer {
    std::string name;
    AssetRef<AnimationClip> clip;
    AnimationLayerMode mode = AnimationLayerMode::Override;
    float weight = 1;
    float speed = 1;
    bool loop = true;
    // Empty includes every bone. Named entries override earlier entries.
    std::vector<BoneMaskEntry> mask;
    void Serialize(Archive& archive);
};

bool BuildBoneMask(const Skeleton& skeleton, std::span<const BoneMaskEntry> entries, std::span<float> weights);
void ApplyAnimationLayer(std::span<const TransformComponents> base, std::span<const TransformComponents> layer,
                         std::span<const TransformComponents> reference, std::span<const float> mask, float weight,
                         AnimationLayerMode mode, std::span<TransformComponents> result);

} // namespace Spot
