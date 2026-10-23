#include "spot/assets/asset_formats.h"

#include <utility>

namespace Spot::Detail {

void Encode(BinaryWriter& writer, const Skeleton& skeleton) {
    writer.WriteString(skeleton.name);
    writer.Write(static_cast<std::uint32_t>(skeleton.bones.size()));
    for (const auto& bone : skeleton.bones) {
        writer.WriteString(bone.name);
        writer.Write(bone.parent);
        writer.Write(bone.bindPose);
    }
    writer.Write(static_cast<std::uint32_t>(skeleton.joints.size()));
    for (std::size_t i = 0; i < skeleton.joints.size(); ++i) {
        writer.Write(skeleton.joints[i]);
        writer.Write(skeleton.inverseBindMatrices[i]);
    }
}

std::optional<Skeleton> DecodeSkeleton(std::span<const std::byte> data) {
    BinaryReader reader(data);
    Skeleton skeleton;
    skeleton.name = reader.ReadString();
    const auto bones = reader.Read<std::uint32_t>();
    for (std::uint32_t i = 0; i < bones && reader.IsValid(); ++i) {
        SkeletonBone bone;
        bone.name = reader.ReadString();
        bone.parent = reader.Read<std::int32_t>();
        bone.bindPose = reader.Read<TransformComponents>();
        skeleton.bones.push_back(std::move(bone));
    }
    const auto joints = reader.Read<std::uint32_t>();
    for (std::uint32_t i = 0; i < joints && reader.IsValid(); ++i) {
        skeleton.joints.push_back(reader.Read<std::uint32_t>());
        skeleton.inverseBindMatrices.push_back(reader.Read<Matrix4>());
    }
    return reader.IsValid() && reader.IsAtEnd() && skeleton.IsValid() ? std::optional(std::move(skeleton))
                                                                      : std::nullopt;
}

void Encode(BinaryWriter& writer, const AnimationClip& clip) {
    writer.WriteString(clip.name);
    writer.Write(clip.duration);
    writer.Write(static_cast<std::uint32_t>(clip.channels.size()));
    for (const auto& channel : clip.channels) {
        writer.Write(channel.bone);
        writer.Write(static_cast<std::uint8_t>(channel.path));
        writer.Write(static_cast<std::uint8_t>(channel.interpolation));
        writer.Write(static_cast<std::uint32_t>(channel.times.size()));
        for (const float time : channel.times) {
            writer.Write(time);
        }
        writer.Write(static_cast<std::uint32_t>(channel.values.size()));
        for (const auto& value : channel.values) {
            writer.Write(value);
        }
    }
    writer.Write(static_cast<std::uint32_t>(clip.events.size()));
    for (const auto& event : clip.events) {
        writer.Write(event.time);
        writer.WriteString(event.name);
        writer.WriteString(event.payload);
    }
}

std::optional<AnimationClip> DecodeAnimationClip(std::span<const std::byte> data) {
    BinaryReader reader(data);
    AnimationClip clip;
    clip.name = reader.ReadString();
    clip.duration = reader.Read<float>();
    const auto channels = reader.Read<std::uint32_t>();
    for (std::uint32_t i = 0; i < channels && reader.IsValid(); ++i) {
        AnimationChannel channel;
        channel.bone = reader.Read<std::uint32_t>();
        channel.path = static_cast<AnimationPath>(reader.Read<std::uint8_t>());
        channel.interpolation = static_cast<AnimationInterpolation>(reader.Read<std::uint8_t>());
        const auto times = reader.Read<std::uint32_t>();
        for (std::uint32_t j = 0; j < times && reader.IsValid(); ++j) {
            channel.times.push_back(reader.Read<float>());
        }
        const auto values = reader.Read<std::uint32_t>();
        for (std::uint32_t j = 0; j < values && reader.IsValid(); ++j) {
            channel.values.push_back(reader.Read<Vector4>());
        }
        clip.channels.push_back(std::move(channel));
    }
    const auto events = reader.Read<std::uint32_t>();
    for (std::uint32_t i = 0; i < events && reader.IsValid(); ++i) {
        AnimationEvent event;
        event.time = reader.Read<float>();
        event.name = reader.ReadString();
        event.payload = reader.ReadString();
        clip.events.push_back(std::move(event));
    }
    return reader.IsValid() && reader.IsAtEnd() && clip.IsValid() ? std::optional(std::move(clip)) : std::nullopt;
}

} // namespace Spot::Detail
