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
    // The rig the clip was made for, whose bones channels address: every node of the file it came from, as
    // Skeleton::bones lists them. Clips play on other skeletons through it, matched by bone names (see
    // RetargetAnimationClip()); empty when channels address the bones of the skeleton they play on, as in clips made
    // in code.
    std::vector<SkeletonBone> rig = {};
    // On a rig other than its own, every bone but the moving root keeps its own length, and the root's movement is
    // scaled to the rig's size, so a clip made for one character fits another; off keeps every translation as the
    // clip has it.
    bool retargetTranslations = true;
    // Root motion turns the entity as the root bone turns about the up axis; off leaves the turning in the pose, as
    // straight walks that sway their hips want.
    bool rootMotionRotation = true;

    bool IsValid() const;
};

} // namespace Spot
