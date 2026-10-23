#include "spot/animation/blend_tree.h"

#include "spot/animation/pose.h"
#include "spot/assets/asset_archive.h"
#include "spot/core/angles.h"
#include "spot/core/archive.h"
#include "spot/core/assert.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Spot {

void AnimationMotion::Serialize(Archive& archive) {
    archive.Field("clip", clip);
    archive.Field("position", position);
    archive.Field("speed", speed);
}
void BlendTree::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 3> Types{"Clip", "1D", "2D directional"};
    archive.Field("type", type, {.enumNames = Types});
    archive.Field("parameterX", parameterX);
    archive.Field("parameterY", parameterY);
    archive.Field("motions", motions);
}
bool BlendTree::IsValid() const {
    if (type == BlendTreeType::None) {
        return true;
    }
    if (type < BlendTreeType::None || type > BlendTreeType::Directional2D || motions.empty() || parameterX.empty() ||
        (type == BlendTreeType::Directional2D && parameterY.empty())) {
        return false;
    }
    for (std::size_t i = 0; i < motions.size(); ++i) {
        const auto& motion = motions[i];
        if ((!motion.clip && !motion.clip.GetId().IsValid()) || (motion.clip && !motion.clip->IsValid()) ||
            !std::isfinite(motion.position.x) || !std::isfinite(motion.position.y) || !std::isfinite(motion.speed) ||
            motion.speed <= 0) {
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (motion.position.x == motions[j].position.x &&
                (type == BlendTreeType::OneDimensional || motion.position.y == motions[j].position.y)) {
                return false;
            }
        }
    }
    return true;
}

void ComputeBlendWeights(const BlendTree& tree, Vector2 value, std::span<float> weights) {
    SPOT_ASSERT(weights.size() == tree.motions.size(), "Blend weight count must match motions");
    std::ranges::fill(weights, 0.0f);
    if (weights.empty()) {
        return;
    }
    if (!std::isfinite(value.x) || !std::isfinite(value.y)) {
        value = {};
    }
    std::size_t nearest = 0;
    float nearestDistance = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < tree.motions.size(); ++i) {
        const auto position = tree.motions[i].position;
        const float distance = tree.type == BlendTreeType::OneDimensional ? std::abs(value.x - position.x)
                                                                          : LengthSquared(value - position);
        if (distance < nearestDistance) {
            nearest = i;
            nearestDistance = distance;
        }
        if (distance < 1e-6f) {
            weights[i] = 1;
            return;
        }
    }
    if (tree.type == BlendTreeType::OneDimensional) {
        std::optional<std::size_t> lower, upper;
        for (std::size_t i = 0; i < tree.motions.size(); ++i) {
            const float x = tree.motions[i].position.x;
            if (x < value.x && (!lower || x > tree.motions[*lower].position.x)) {
                lower = i;
            }
            if (x > value.x && (!upper || x < tree.motions[*upper].position.x)) {
                upper = i;
            }
        }
        if (lower && upper) {
            const float t = (value.x - tree.motions[*lower].position.x) /
                            (tree.motions[*upper].position.x - tree.motions[*lower].position.x);
            weights[*lower] = 1 - t;
            weights[*upper] = t;
        } else {
            weights[nearest] = 1;
        }
        return;
    }
    const float radius = Length(value);
    float sum = 0;
    for (std::size_t i = 0; i < tree.motions.size(); ++i) {
        const auto a = tree.motions[i].position;
        const float ra = Length(a);
        float weight = 1;
        for (std::size_t j = 0; j < tree.motions.size(); ++j) {
            if (i == j) {
                continue;
            }
            const auto b = tree.motions[j].position;
            const float rb = Length(b);
            float band;
            if (ra < 1e-6f) {
                band = 1 - radius / std::max(rb, 1e-6f);
            } else if (rb < 1e-6f) {
                band = radius / ra;
            } else {
                const float meanRadius = (ra + rb) * 0.5f;
                const float angle = std::atan2(Cross(Vector3{a.x, a.y, 0}, Vector3{b.x, b.y, 0}).z, Dot(a, b));
                const float sampleAngle =
                    radius < 1e-6f
                        ? 0
                        : std::atan2(Cross(Vector3{a.x, a.y, 0}, Vector3{value.x, value.y, 0}).z, Dot(a, value));
                const Vector2 delta{(rb - ra) / meanRadius, angle};
                const Vector2 sample{(radius - ra) / meanRadius, sampleAngle};
                band = 1 - Dot(sample, delta) / std::max(LengthSquared(delta), 1e-6f);
            }
            weight = std::min(weight, std::clamp(band, 0.0f, 1.0f));
        }
        weights[i] = weight;
        sum += weight;
    }
    if (sum > 1e-6f) {
        for (float& weight : weights) {
            weight /= sum;
        }
    } else {
        weights[nearest] = 1;
    }
}

float GetBlendDuration(const BlendTree& tree, std::span<const float> weights) {
    float duration = 0;
    for (std::size_t i = 0; i < tree.motions.size(); ++i) {
        const auto& motion = tree.motions[i];
        if (motion.clip && motion.speed > 0) {
            duration += weights[i] * motion.clip->duration / motion.speed;
        }
    }
    return duration;
}

void SampleBlendTree(const Skeleton& skeleton, const BlendTree& tree, Vector2 value, float time, bool loop,
                     std::span<TransformComponents> result, std::span<TransformComponents> scratch,
                     std::span<float> weights) {
    ComputeBlendWeights(tree, value, weights);
    const float duration = GetBlendDuration(tree, weights);
    const float phase = duration > 0 ? time / duration : 0;
    Pose::Bind(skeleton, result);
    float accumulated = 0;
    for (std::size_t i = 0; i < tree.motions.size(); ++i) {
        const auto& motion = tree.motions[i];
        if (weights[i] <= 0 || !motion.clip) {
            continue;
        }
        Pose::Sample(skeleton, *motion.clip, phase * motion.clip->duration, loop, scratch);
        accumulated += weights[i];
        Pose::Blend(result, scratch, weights[i] / accumulated, result);
    }
}

} // namespace Spot
