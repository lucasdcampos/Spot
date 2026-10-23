#pragma once

#include "spot/animation/animation_data.h"
#include "spot/core/asset_ref.h"

#include <span>
#include <string>
#include <vector>

namespace Spot {

class Archive;
enum class BlendTreeType { None, OneDimensional, Directional2D };

struct AnimationMotion {
    AssetRef<AnimationClip> clip;
    Vector2 position;
    float speed = 1;
    void Serialize(Archive& archive);
};

struct BlendTree {
    BlendTreeType type = BlendTreeType::None;
    std::string parameterX;
    std::string parameterY;
    std::vector<AnimationMotion> motions;

    bool IsValid() const;
    void Serialize(Archive& archive);
};

void ComputeBlendWeights(const BlendTree& tree, Vector2 value, std::span<float> weights);
float GetBlendDuration(const BlendTree& tree, std::span<const float> weights);
void SampleBlendTree(const Skeleton& skeleton, const BlendTree& tree, Vector2 value, float time, bool loop,
                     std::span<TransformComponents> result, std::span<TransformComponents> scratch,
                     std::span<float> weights);

} // namespace Spot
