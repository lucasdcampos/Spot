#pragma once

#include "spot/core/math/matrix.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Spot {

class Archive;

struct SkeletonBone {
    std::string name;
    std::int32_t parent = -1;
    TransformComponents bindPose;
};

// Bones include intermediary nodes; joints preserve the skin's vertex-index order.
struct Skeleton {
    std::string name;
    std::vector<SkeletonBone> bones;
    std::vector<std::uint32_t> joints;
    std::vector<Matrix4> inverseBindMatrices;

    bool IsValid() const;
    std::optional<std::size_t> FindBone(std::string_view bone) const;
};

enum class AnimationPath { Translation, Rotation, Scale };
enum class AnimationInterpolation { Step, Linear, Cubic };

struct AnimationChannel {
    std::uint32_t bone = 0;
    AnimationPath path = AnimationPath::Translation;
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    std::vector<float> times;
    // Cubic keys are in-tangent, value, out-tangent, in glTF order.
    std::vector<Vector4> values;
};

struct AnimationEvent {
    float time = 0.0f;
    std::string name;
    std::string payload;
    void Serialize(Archive& archive);
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationChannel> channels;
    std::vector<AnimationEvent> events = {};

    bool IsValid() const;
};

} // namespace Spot
