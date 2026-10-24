#include "spot/animation/animation_data.h"

#include "spot/core/io/archive.h"

#include <cmath>

namespace Spot {

namespace {
bool IsFinite(Vector4 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}
} // namespace

void AnimationEvent::Serialize(Archive& archive) {
    archive.Field("time", time);
    archive.Field("name", name);
    archive.Field("payload", payload);
}

bool Skeleton::IsValid() const {
    if (bones.empty() || joints.size() != inverseBindMatrices.size()) {
        return false;
    }
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const auto& bone = bones[i];
        if (bone.parent < -1 || (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) >= i)) {
            return false;
        }
        const auto& pose = bone.bindPose;
        if (!IsFinite({pose.translation.x, pose.translation.y, pose.translation.z, 0}) ||
            !IsFinite({pose.rotation.x, pose.rotation.y, pose.rotation.z, pose.rotation.w}) ||
            !IsFinite({pose.scale.x, pose.scale.y, pose.scale.z, 0})) {
            return false;
        }
    }
    for (std::size_t i = 0; i < joints.size(); ++i) {
        if (joints[i] >= bones.size()) {
            return false;
        }
        for (const auto& column : inverseBindMatrices[i].columns) {
            if (!IsFinite(column)) {
                return false;
            }
        }
    }
    return true;
}

std::optional<std::size_t> Skeleton::FindBone(std::string_view bone) const {
    for (std::size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name == bone) {
            return i;
        }
    }
    return std::nullopt;
}

bool AnimationClip::IsValid() const {
    if (!std::isfinite(duration) || duration < 0.0f) {
        return false;
    }
    for (const auto& channel : channels) {
        const auto count = channel.times.size();
        if (count == 0 || channel.path > AnimationPath::Scale ||
            channel.interpolation > AnimationInterpolation::Cubic ||
            channel.values.size() != count * (channel.interpolation == AnimationInterpolation::Cubic ? 3 : 1)) {
            return false;
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(channel.times[i]) || channel.times[i] < 0.0f || channel.times[i] > duration ||
                (i && channel.times[i] <= channel.times[i - 1])) {
                return false;
            }
        }
        for (const auto& value : channel.values) {
            if (!IsFinite(value)) {
                return false;
            }
        }
    }
    float previousEvent = -1;
    for (const auto& event : events) {
        if (!std::isfinite(event.time) || event.time < 0.0f || event.time > duration || event.time < previousEvent) {
            return false;
        }
        previousEvent = event.time;
    }
    return true;
}

} // namespace Spot
