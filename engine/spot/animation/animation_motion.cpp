#include "spot/animation/animation_motion.h"

#include "spot/animation/pose.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Spot {

std::size_t CollectAnimationEvents(const AnimationClip& clip, float from, float to, bool loop,
                                   std::span<const AnimationEvent*> result, bool includeStart) {
    if (clip.events.empty() || result.empty() || !std::isfinite(from) || !std::isfinite(to)) {
        return 0;
    }
    std::size_t count = 0;
    if (!(clip.duration > 0)) {
        if (includeStart) {
            for (const auto& event : clip.events) {
                result[count++] = &event;
                if (count == result.size()) {
                    break;
                }
            }
        }
        return count;
    }
    if (!loop) {
        from = Pose::WrapTime(from, clip.duration, false);
        to = Pose::WrapTime(to, clip.duration, false);
    }
    const bool forward = to >= from;
    const double duration = clip.duration;
    const double first = loop ? std::floor(static_cast<double>(from) / duration) : 0;
    const double last = loop ? std::floor(static_cast<double>(to) / duration) : 0;
    // Each full cycle has events; the bounded output also bounds the number of cycles visited.
    const double cycles = std::min(std::abs(last - first), static_cast<double>(result.size()) + 1);
    for (std::size_t cycle = 0; cycle <= static_cast<std::size_t>(cycles); ++cycle) {
        const double base = (first + (forward ? static_cast<double>(cycle) : -static_cast<double>(cycle))) * duration;
        for (std::size_t i = 0; i < clip.events.size(); ++i) {
            const auto index = forward ? i : clip.events.size() - 1 - i;
            const auto& event = clip.events[index];
            const double time = base + event.time;
            const bool crossed = forward ? (time > from && time <= to) : (time >= to && time < from);
            if (crossed || (includeStart && time == from)) {
                result[count++] = &event;
                if (count == result.size()) {
                    return count;
                }
            }
        }
    }
    return count;
}

namespace {
Matrix4 Root(const Skeleton& skeleton, const AnimationClip& clip, std::size_t bone, float time) {
    auto pose = skeleton.bones[bone].bindPose;
    for (const auto& channel : clip.channels) {
        if (channel.bone != bone) {
            continue;
        }
        const auto value = Pose::SampleChannel(channel, time);
        if (channel.path == AnimationPath::Translation) {
            pose.translation = {value.x, value.y, value.z};
        } else if (channel.path == AnimationPath::Rotation) {
            pose.rotation = {value.x, value.y, value.z, value.w};
        }
    }
    return Matrix4::Translation(pose.translation) * Matrix4::Rotation(pose.rotation);
}

Matrix4 Power(Matrix4 matrix, double exponent) {
    if (exponent < 0) {
        matrix = Inverse(matrix);
        exponent = -exponent;
    }
    Matrix4 result;
    while (exponent >= 1) {
        if (std::fmod(exponent, 2.0) >= 1) {
            result = result * matrix;
        }
        exponent = std::floor(exponent / 2);
        matrix = matrix * matrix;
    }
    return result;
}
} // namespace

TransformComponents GetRootMotionDelta(const Skeleton& skeleton, const AnimationClip& clip, std::size_t bone,
                                       float from, float to, bool loop) {
    if (bone >= skeleton.bones.size() || !std::isfinite(from) || !std::isfinite(to) || !(clip.duration > 0)) {
        return {};
    }
    const auto start = Inverse(Root(skeleton, clip, bone, 0));
    const auto cycle = start * Root(skeleton, clip, bone, clip.duration);
    const auto absolute = [&](float time) {
        const auto phase = start * Root(skeleton, clip, bone, Pose::WrapTime(time, clip.duration, loop));
        return loop ? Power(cycle, std::floor(static_cast<double>(time) / clip.duration)) * phase : phase;
    };
    return Decompose(Inverse(absolute(from)) * absolute(to));
}

} // namespace Spot
