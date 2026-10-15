#include "animation/animation_layer.h"

#include "assets/asset_archive.h"
#include "core/archive.h"
#include "core/assert.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Spot {

void BoneMaskEntry::Serialize(Archive& archive) {
    archive.Field("bone", bone);
    archive.Field("weight", weight, {.min = 0, .max = 1});
    archive.Field("descendants", descendants);
}
void AnimationLayer::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 2> Modes{"Override", "Additive"};
    archive.Field("name", name);
    archive.Field("clip", clip);
    archive.Field("mode", mode, {.enumNames = Modes});
    archive.Field("weight", weight, {.min = 0, .max = 1});
    archive.Field("speed", speed);
    archive.Field("loop", loop);
    archive.Field("mask", mask);
}

bool BuildBoneMask(const Skeleton& skeleton, std::span<const BoneMaskEntry> entries, std::span<float> weights) {
    SPOT_ASSERT(weights.size() == skeleton.bones.size(), "Mask size must match bones");
    std::ranges::fill(weights, entries.empty() ? 1.0f : 0.0f);
    for (const auto& entry : entries) {
        const auto bone = skeleton.FindBone(entry.bone);
        if (!bone || !std::isfinite(entry.weight)) {
            return false;
        }
        const float weight = std::clamp(entry.weight, 0.0f, 1.0f);
        for (std::size_t i = 0; i < weights.size(); ++i) {
            auto ancestor = static_cast<std::int32_t>(i);
            while (ancestor >= 0) {
                if (static_cast<std::size_t>(ancestor) == *bone) {
                    weights[i] = weight;
                    break;
                }
                if (!entry.descendants) {
                    break;
                }
                ancestor = skeleton.bones[static_cast<std::size_t>(ancestor)].parent;
            }
        }
    }
    return true;
}

void ApplyAnimationLayer(std::span<const TransformComponents> base, std::span<const TransformComponents> layer,
                         std::span<const TransformComponents> reference, std::span<const float> mask, float weight,
                         AnimationLayerMode mode, std::span<TransformComponents> result) {
    SPOT_ASSERT(base.size() == layer.size() && base.size() == reference.size() && base.size() == mask.size() &&
                    base.size() == result.size(),
                "Layer pose sizes must match");
    for (std::size_t i = 0; i < result.size(); ++i) {
        const float influence = std::clamp(weight * mask[i], 0.0f, 1.0f);
        if (mode == AnimationLayerMode::Override) {
            result[i] = {Lerp(base[i].translation, layer[i].translation, influence),
                         Slerp(base[i].rotation, layer[i].rotation, influence),
                         Lerp(base[i].scale, layer[i].scale, influence)};
        } else {
            const auto ratio = [](float value, float referenceValue) {
                return referenceValue != 0 ? value / referenceValue : 1;
            };
            const Vector3 scaleRatio{ratio(layer[i].scale.x, reference[i].scale.x),
                                     ratio(layer[i].scale.y, reference[i].scale.y),
                                     ratio(layer[i].scale.z, reference[i].scale.z)};
            result[i] = {base[i].translation + (layer[i].translation - reference[i].translation) * influence,
                         Normalize(base[i].rotation *
                                   Slerp(Quaternion{}, Inverse(reference[i].rotation) * layer[i].rotation, influence)),
                         base[i].scale * Lerp(Vector3{1, 1, 1}, scaleRatio, influence)};
        }
    }
}

} // namespace Spot
