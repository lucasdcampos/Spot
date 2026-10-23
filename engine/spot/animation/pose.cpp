#include "spot/animation/pose.h"

#include "spot/core/assert.h"

#include <algorithm>
#include <cmath>

namespace Spot::Pose {

float WrapTime(float time, float duration, bool loop) {
    if (!(duration > 0) || !std::isfinite(time)) {
        return 0;
    }
    if (!loop) {
        return std::clamp(time, 0.0f, duration);
    }
    const float wrapped = std::fmod(time, duration);
    return wrapped < 0 ? wrapped + duration : wrapped;
}

Vector4 SampleChannel(const AnimationChannel& channel, float time) {
    if (channel.times.empty()) {
        return {};
    }
    const bool cubic = channel.interpolation == AnimationInterpolation::Cubic;
    const auto value = [&](std::size_t i) { return channel.values[cubic ? i * 3 + 1 : i]; };
    const auto finish = [&](Vector4 v) {
        if (channel.path == AnimationPath::Rotation) {
            const auto q = Normalize(Quaternion{v.x, v.y, v.z, v.w});
            return Vector4{q.x, q.y, q.z, q.w};
        }
        return v;
    };
    if (time <= channel.times.front()) {
        return finish(value(0));
    }
    if (time >= channel.times.back()) {
        return finish(value(channel.times.size() - 1));
    }
    const auto next = static_cast<std::size_t>(std::upper_bound(channel.times.begin(), channel.times.end(), time) -
                                               channel.times.begin());
    const auto previous = next - 1;
    const float duration = channel.times[next] - channel.times[previous];
    const float t = (time - channel.times[previous]) / duration;
    const auto a = value(previous), b = value(next);
    if (channel.interpolation == AnimationInterpolation::Step) {
        return finish(a);
    }
    if (cubic) {
        const float t2 = t * t, t3 = t2 * t;
        return finish(a * (2 * t3 - 3 * t2 + 1) + channel.values[previous * 3 + 2] * (duration * (t3 - 2 * t2 + t)) +
                      b * (-2 * t3 + 3 * t2) + channel.values[next * 3] * (duration * (t3 - t2)));
    }
    if (channel.path == AnimationPath::Rotation) {
        const auto q = Slerp({a.x, a.y, a.z, a.w}, {b.x, b.y, b.z, b.w}, t);
        return {q.x, q.y, q.z, q.w};
    }
    return Lerp(a, b, t);
}

void Bind(const Skeleton& skeleton, std::span<TransformComponents> local) {
    SPOT_ASSERT(local.size() == skeleton.bones.size(), "Pose size must match skeleton");
    for (std::size_t i = 0; i < local.size(); ++i) {
        local[i] = skeleton.bones[i].bindPose;
    }
}

void Sample(const Skeleton& skeleton, const AnimationClip& clip, float time, bool loop,
            std::span<TransformComponents> local) {
    Bind(skeleton, local);
    time = WrapTime(time, clip.duration, loop);
    for (const auto& channel : clip.channels) {
        if (channel.bone >= local.size()) {
            continue;
        }
        const Vector4 value = SampleChannel(channel, time);
        auto& bone = local[channel.bone];
        switch (channel.path) {
        case AnimationPath::Translation:
            bone.translation = {value.x, value.y, value.z};
            break;
        case AnimationPath::Rotation:
            bone.rotation = {value.x, value.y, value.z, value.w};
            break;
        case AnimationPath::Scale:
            bone.scale = {value.x, value.y, value.z};
            break;
        }
    }
}

void Blend(std::span<const TransformComponents> a, std::span<const TransformComponents> b, float weight,
           std::span<TransformComponents> result) {
    SPOT_ASSERT(a.size() == b.size() && a.size() == result.size(), "Blended poses must have equal sizes");
    weight = std::clamp(weight, 0.0f, 1.0f);
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = {Lerp(a[i].translation, b[i].translation, weight), Slerp(a[i].rotation, b[i].rotation, weight),
                     Lerp(a[i].scale, b[i].scale, weight)};
    }
}

void ToModel(const Skeleton& skeleton, std::span<const TransformComponents> local, std::span<Matrix4> model) {
    SPOT_ASSERT(local.size() == skeleton.bones.size() && model.size() == local.size(), "Pose size must match skeleton");
    for (std::size_t i = 0; i < local.size(); ++i) {
        const auto& bone = local[i];
        auto matrix =
            Matrix4::Translation(bone.translation) * Matrix4::Rotation(bone.rotation) * Matrix4::Scale(bone.scale);
        const auto parent = skeleton.bones[i].parent;
        model[i] = parent >= 0 ? model[static_cast<std::size_t>(parent)] * matrix : matrix;
    }
}

void Palette(const Skeleton& skeleton, std::span<const Matrix4> model, const Matrix4& meshModel,
             std::span<Matrix4> palette) {
    SPOT_ASSERT(palette.size() == skeleton.joints.size(), "Palette size must match joints");
    const auto inverseMesh = Inverse(meshModel);
    for (std::size_t i = 0; i < palette.size(); ++i) {
        palette[i] = inverseMesh * model[skeleton.joints[i]] * skeleton.inverseBindMatrices[i];
    }
}

} // namespace Spot::Pose
