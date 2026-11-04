#include "spot/animation/animation_binding.h"

#include "spot/animation/pose.h"
#include "spot/core/math/angles.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <mutex>
#include <vector>

namespace Spot {

namespace {

bool EqualIgnoringCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

bool Near(Vector3 a, Vector3 b) {
    return Length(a - b) <= 1e-4f * std::max(1.0f, Length(a));
}

// Whether the channel's value changes, rather than holding one value the whole clip, as exporters key every bone.
bool Moves(const AnimationChannel& channel) {
    const std::size_t stride = channel.interpolation == AnimationInterpolation::Cubic ? 3 : 1;
    const std::size_t offset = stride == 3 ? 1 : 0;
    const Vector4 first = channel.values[offset];
    const float tolerance = 1e-5f * std::max(1.0f, Length(first));
    for (std::size_t i = offset; i < channel.values.size(); i += stride) {
        if (Length(channel.values[i] - first) > tolerance) {
            return true;
        }
    }
    return false;
}

std::optional<std::size_t> FindTarget(const Skeleton& skeleton, std::string_view name) {
    if (const auto exact = skeleton.FindBone(name)) {
        return exact;
    }
    const std::string_view base = GetBoneBaseName(name);
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        if (EqualIgnoringCase(GetBoneBaseName(skeleton.bones[i].name), base)) {
            return i;
        }
    }
    return std::nullopt;
}

bool HasAncestor(const Skeleton& skeleton, std::size_t bone, const std::vector<bool>& among) {
    for (auto parent = skeleton.bones[bone].parent; parent >= 0;
         parent = skeleton.bones[static_cast<std::size_t>(parent)].parent) {
        if (among[static_cast<std::size_t>(parent)]) {
            return true;
        }
    }
    return false;
}

constexpr auto HumanBoneCount = static_cast<std::size_t>(HumanBone::Count);

// By HumanBone: the common name, then Mixamo's where it is another.
constexpr std::array<std::array<std::string_view, 2>, HumanBoneCount> HumanBoneNames = {{
    {"Hips", "Pelvis"},
    {"Spine", ""},
    {"Chest", "Spine2"},
    {"Neck", ""},
    {"Head", ""},
    {"LeftShoulder", ""},
    {"LeftUpperArm", "LeftArm"},
    {"LeftLowerArm", "LeftForeArm"},
    {"LeftHand", ""},
    {"RightShoulder", ""},
    {"RightUpperArm", "RightArm"},
    {"RightLowerArm", "RightForeArm"},
    {"RightHand", ""},
    {"LeftUpperLeg", "LeftUpLeg"},
    {"LeftLowerLeg", "LeftLeg"},
    {"LeftFoot", ""},
    {"LeftToes", "LeftToeBase"},
    {"RightUpperLeg", "RightUpLeg"},
    {"RightLowerLeg", "RightLeg"},
    {"RightFoot", ""},
    {"RightToes", "RightToeBase"},
}};

using HumanBones = std::array<std::optional<std::size_t>, HumanBoneCount>;

HumanBones FindHumanBones(std::span<const SkeletonBone> bones) {
    HumanBones found;
    for (std::size_t part = 0; part < HumanBoneCount; ++part) {
        found[part] = FindHumanBone(bones, static_cast<HumanBone>(part));
    }
    return found;
}

// Whether the rig has a whole body to carry a clip over: what the spine, the chest, the shoulders and the toes do
// follows from the rest.
bool IsHuman(const HumanBones& bones) {
    for (const HumanBone part :
         {HumanBone::Hips, HumanBone::Head, HumanBone::LeftUpperArm, HumanBone::LeftLowerArm, HumanBone::LeftHand,
          HumanBone::RightUpperArm, HumanBone::RightLowerArm, HumanBone::RightHand, HumanBone::LeftUpperLeg,
          HumanBone::LeftLowerLeg, HumanBone::LeftFoot, HumanBone::RightUpperLeg, HumanBone::RightLowerLeg,
          HumanBone::RightFoot}) {
        if (!bones[static_cast<std::size_t>(part)]) {
            return false;
        }
    }
    return true;
}

bool SameBoneNames(const AnimationClip& clip, const Skeleton& skeleton, const HumanBones& source,
                   const HumanBones& target) {
    for (std::size_t part = 0; part < HumanBoneCount; ++part) {
        if (source[part] && target[part] &&
            !EqualIgnoringCase(GetBoneBaseName(clip.rig[*source[part]].name),
                               GetBoneBaseName(skeleton.bones[*target[part]].name))) {
            return false;
        }
    }
    return true;
}

Quaternion ShortestTurn(Vector3 from, Vector3 to) {
    from = Normalize(from);
    to = Normalize(to);
    const float cosine = Dot(from, to);
    if (cosine < -0.9999f) {
        const Vector3 side = std::abs(from.x) < 0.9f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
        return Quaternion::FromAxisAngle(Normalize(Cross(from, side)), Pi);
    }
    const Vector3 axis = Cross(from, to);
    return Normalize(Quaternion{axis.x, axis.y, axis.z, 1 + cosine});
}

// The way a body at rest faces about the up axis, seen from its legs: the right one is on its right.
float GetFacingYaw(std::span<const TransformComponents> rest, const HumanBones& bones) {
    const Vector3 right = rest[*bones[static_cast<std::size_t>(HumanBone::RightUpperLeg)]].translation -
                          rest[*bones[static_cast<std::size_t>(HumanBone::LeftUpperLeg)]].translation;
    const Vector3 forward = Cross(Vector3{0, 1, 0}, right);
    return std::atan2(forward.x, forward.z);
}

std::vector<TransformComponents> DecomposePose(std::span<const Matrix4> model) {
    std::vector<TransformComponents> pose;
    pose.reserve(model.size());
    for (const Matrix4& matrix : model) {
        pose.push_back(Decompose(matrix));
    }
    return pose;
}

AnimationClip RetargetHumanClip(const AnimationClip& clip, const Skeleton& skeleton, const HumanBones& source,
                                const HumanBones& target) {
    Skeleton rig;
    rig.bones = clip.rig;
    std::vector<TransformComponents> local(rig.bones.size());
    std::vector<Matrix4> model(rig.bones.size());
    Pose::Bind(rig, local);
    Pose::ToModel(rig, local, model);
    const std::vector<TransformComponents> sourceRest = DecomposePose(model);

    std::vector<TransformComponents> targetLocal(skeleton.bones.size());
    std::vector<Matrix4> targetModel(skeleton.bones.size());
    Pose::Bind(skeleton, targetLocal);
    Pose::ToModel(skeleton, targetLocal, targetModel);
    const std::vector<TransformComponents> targetRest = DecomposePose(targetModel);

    const Quaternion facing =
        Quaternion::FromAxisAngle({0, 1, 0}, GetFacingYaw(targetRest, target) - GetFacingYaw(sourceRest, source));

    // The skeleton's rest pose with its arms and legs laid as the clip's rest: a T-pose takes the clip's hanging arms.
    // Hands follow their forearms, and the rest stays as it is: feet stand flat in both, whatever their bones' angle.
    std::array<Quaternion, HumanBoneCount> restTurn;
    const auto turnLimb = [&](HumanBone part, HumanBone end) {
        const auto from = static_cast<std::size_t>(part), to = static_cast<std::size_t>(end);
        if (source[from] && source[to] && target[from] && target[to]) {
            const Vector3 sourceLimb = sourceRest[*source[to]].translation - sourceRest[*source[from]].translation;
            const Vector3 targetLimb = targetRest[*target[to]].translation - targetRest[*target[from]].translation;
            restTurn[from] = ShortestTurn(targetLimb, facing * sourceLimb);
        }
    };
    turnLimb(HumanBone::LeftUpperArm, HumanBone::LeftLowerArm);
    turnLimb(HumanBone::LeftLowerArm, HumanBone::LeftHand);
    turnLimb(HumanBone::RightUpperArm, HumanBone::RightLowerArm);
    turnLimb(HumanBone::RightLowerArm, HumanBone::RightHand);
    turnLimb(HumanBone::LeftUpperLeg, HumanBone::LeftLowerLeg);
    turnLimb(HumanBone::LeftLowerLeg, HumanBone::LeftFoot);
    turnLimb(HumanBone::RightUpperLeg, HumanBone::RightLowerLeg);
    turnLimb(HumanBone::RightLowerLeg, HumanBone::RightFoot);
    restTurn[static_cast<std::size_t>(HumanBone::LeftHand)] =
        restTurn[static_cast<std::size_t>(HumanBone::LeftLowerArm)];
    restTurn[static_cast<std::size_t>(HumanBone::RightHand)] =
        restTurn[static_cast<std::size_t>(HumanBone::RightLowerArm)];

    const auto hips = static_cast<std::size_t>(HumanBone::Hips);
    const auto legHeight = [&](const std::vector<TransformComponents>& rest, const HumanBones& bones) {
        return rest[*bones[hips]].translation.y -
               rest[*bones[static_cast<std::size_t>(HumanBone::LeftFoot)]].translation.y;
    };
    const float sourceHeight = legHeight(sourceRest, source), targetHeight = legHeight(targetRest, target);
    const float hipScale = clip.retargetTranslations && sourceHeight > 1e-4f && targetHeight > 1e-4f
                               ? targetHeight / sourceHeight
                               : 1.0f;

    // The clip's keys, so what it does between them is what it did.
    std::vector<float> times = {0.0f};
    bool hipsMove = false;
    for (const auto& channel : clip.channels) {
        times.insert(times.end(), channel.times.begin(), channel.times.end());
        hipsMove = hipsMove || (channel.path == AnimationPath::Translation && channel.bone == *source[hips]);
    }
    std::ranges::sort(times);
    times.erase(std::unique(times.begin(), times.end()), times.end());

    AnimationClip result;
    result.name = clip.name;
    result.duration = clip.duration;
    result.events = clip.events;
    result.retargetTranslations = clip.retargetTranslations;
    result.rootMotionRotation = clip.rootMotionRotation;

    std::vector<int> partOfBone(skeleton.bones.size(), -1);
    std::array<std::size_t, HumanBoneCount> channelOfPart{};
    for (std::size_t part = 0; part < HumanBoneCount; ++part) {
        if (source[part] && target[part]) {
            partOfBone[*target[part]] = static_cast<int>(part);
            channelOfPart[part] = result.channels.size();
            result.channels.push_back({static_cast<std::uint32_t>(*target[part]),
                                       AnimationPath::Rotation,
                                       AnimationInterpolation::Linear,
                                       times,
                                       {}});
        }
    }
    AnimationChannel hipsChannel{
        static_cast<std::uint32_t>(*target[hips]), AnimationPath::Translation, AnimationInterpolation::Linear, times, {}};

    std::vector<Quaternion> rotations(skeleton.bones.size());
    for (const float time : times) {
        Pose::Sample(rig, clip, time, false, local);
        Pose::ToModel(rig, local, model);
        for (std::size_t bone = 0; bone < skeleton.bones.size(); ++bone) {
            const auto parent = skeleton.bones[bone].parent;
            const Quaternion above = parent >= 0 ? rotations[static_cast<std::size_t>(parent)] : Quaternion{};
            const int part = partOfBone[bone];
            if (part < 0) {
                rotations[bone] = Normalize(above * skeleton.bones[bone].bindPose.rotation);
                continue;
            }
            const auto index = static_cast<std::size_t>(part);
            const TransformComponents now = Decompose(model[*source[index]]);
            const TransformComponents& rest = sourceRest[*source[index]];
            const Quaternion turned = facing * now.rotation * Inverse(rest.rotation) * Inverse(facing);
            rotations[bone] = Normalize(turned * restTurn[index] * targetRest[bone].rotation);
            Quaternion value = Normalize(Inverse(above) * rotations[bone]);
            auto& values = result.channels[channelOfPart[index]].values;
            // Keys on one side of the sphere, so that nothing between them goes the long way around.
            if (!values.empty() && values.back().x * value.x + values.back().y * value.y + values.back().z * value.z +
                                           values.back().w * value.w <
                                       0) {
                value = {-value.x, -value.y, -value.z, -value.w};
            }
            values.push_back({value.x, value.y, value.z, value.w});
            if (index == hips && hipsMove) {
                const Vector3 moved = facing * (now.translation - rest.translation) * hipScale;
                const Vector3 translation = skeleton.bones[bone].bindPose.translation + Inverse(above) * moved;
                hipsChannel.values.push_back({translation.x, translation.y, translation.z, 0});
            }
        }
    }
    if (hipsMove) {
        result.channels.push_back(std::move(hipsChannel));
    }
    return result;
}

} // namespace

std::string_view GetBoneBaseName(std::string_view name) {
    const auto separator = name.find_last_of(":|");
    return separator == std::string_view::npos ? name : name.substr(separator + 1);
}

std::optional<std::size_t> FindHumanBone(std::span<const SkeletonBone> bones, HumanBone part) {
    const auto index = static_cast<std::size_t>(part);
    if (index >= HumanBoneCount) {
        return std::nullopt;
    }
    for (const std::string_view name : HumanBoneNames[index]) {
        for (std::size_t bone = 0; !name.empty() && bone < bones.size(); ++bone) {
            if (EqualIgnoringCase(GetBoneBaseName(bones[bone].name), name)) {
                return bone;
            }
        }
    }
    return std::nullopt;
}

bool IsClipForSkeleton(const AnimationClip& clip, const Skeleton& skeleton) {
    if (clip.rig.empty()) {
        return true;
    }
    if (clip.rig.size() != skeleton.bones.size()) {
        return false;
    }
    for (std::size_t i = 0; i < clip.rig.size(); ++i) {
        const auto& source = clip.rig[i];
        const auto& target = skeleton.bones[i];
        if (source.name != target.name || source.parent != target.parent ||
            !Near(source.bindPose.translation, target.bindPose.translation)) {
            return false;
        }
    }
    return true;
}

std::optional<AnimationClip> RetargetAnimationClip(const AnimationClip& clip, const Skeleton& skeleton) {
    if (IsClipForSkeleton(clip, skeleton)) {
        return clip;
    }

    // Rigs that name the same bones alike share their animations by name, below.
    const HumanBones sourceBones = FindHumanBones(clip.rig);
    const HumanBones targetBones = FindHumanBones(skeleton.bones);
    if (IsHuman(sourceBones) && IsHuman(targetBones) && !SameBoneNames(clip, skeleton, sourceBones, targetBones)) {
        return RetargetHumanClip(clip, skeleton, sourceBones, targetBones);
    }

    std::vector<std::optional<std::size_t>> targets(clip.rig.size());
    for (std::size_t i = 0; i < clip.rig.size(); ++i) {
        targets[i] = FindTarget(skeleton, clip.rig[i].name);
    }

    // Exporters key the translation of every bone with the lengths of the character the clip was made for; only the
    // bones that really move, and of those the topmost, such as the hips, carry the character's movement.
    std::vector<bool> moving(skeleton.bones.size(), false);
    for (const auto& channel : clip.channels) {
        if (channel.path == AnimationPath::Translation && targets[channel.bone] && Moves(channel)) {
            moving[*targets[channel.bone]] = true;
        }
    }

    AnimationClip result;
    result.name = clip.name;
    result.duration = clip.duration;
    result.events = clip.events;
    result.retargetTranslations = clip.retargetTranslations;
    result.rootMotionRotation = clip.rootMotionRotation;
    // Two source bones can share a base name; the first keeps the target bone.
    std::vector<std::array<bool, 3>> taken(skeleton.bones.size(), {false, false, false});
    for (const auto& channel : clip.channels) {
        const auto target = targets[channel.bone];
        if (!target || taken[*target][static_cast<std::size_t>(channel.path)]) {
            continue;
        }
        const auto& source = clip.rig[channel.bone].bindPose;
        const auto& bind = skeleton.bones[*target].bindPose;
        AnimationChannel retargeted = channel;
        retargeted.bone = static_cast<std::uint32_t>(*target);
        if (channel.path == AnimationPath::Rotation) {
            // The clip's turn away from its own rest pose, applied to the skeleton's: q * inverse(source) * bind. It
            // is linear in q, so cubic tangents turn the same way.
            const Quaternion correction = Normalize(Inverse(source.rotation) * bind.rotation);
            for (auto& value : retargeted.values) {
                const Quaternion q = Quaternion{value.x, value.y, value.z, value.w} * correction;
                value = {q.x, q.y, q.z, q.w};
            }
        } else if (channel.path == AnimationPath::Translation && clip.retargetTranslations) {
            if (!moving[*target] || HasAncestor(skeleton, *target, moving)) {
                continue;
            }
            const float from = Length(source.translation);
            const float to = Length(bind.translation);
            const float ratio = from > 1e-6f && to > 1e-6f ? to / from : 1.0f;
            for (auto& value : retargeted.values) {
                value = value * ratio;
            }
        }
        taken[*target][static_cast<std::size_t>(channel.path)] = true;
        result.channels.push_back(std::move(retargeted));
    }
    if (std::ranges::none_of(targets, [](const auto& target) { return target.has_value(); })) {
        return std::nullopt;
    }
    return result;
}

std::shared_ptr<const AnimationClip> BindAnimationClip(const std::shared_ptr<const AnimationClip>& clip,
                                                       const std::shared_ptr<const Skeleton>& skeleton) {
    if (!clip || !skeleton) {
        return nullptr;
    }
    if (clip->rig.empty()) {
        return clip;
    }
    struct Binding {
        std::weak_ptr<const AnimationClip> clip;
        std::weak_ptr<const Skeleton> skeleton;
        std::weak_ptr<const AnimationClip> bound;
    };
    static std::mutex mutex;
    static std::vector<Binding> bindings;

    const auto same = [](const auto& weak, const auto& shared) {
        return !weak.owner_before(shared) && !shared.owner_before(weak) && !weak.expired();
    };
    {
        const std::scoped_lock lock(mutex);
        std::erase_if(bindings, [](const Binding& binding) {
            return binding.clip.expired() || binding.skeleton.expired() || binding.bound.expired();
        });
        for (const auto& binding : bindings) {
            if (same(binding.clip, clip) && same(binding.skeleton, skeleton)) {
                return binding.bound.lock();
            }
        }
    }
    if (IsClipForSkeleton(*clip, *skeleton)) {
        return clip;
    }
    auto retargeted = RetargetAnimationClip(*clip, *skeleton);
    if (!retargeted) {
        return nullptr;
    }
    auto bound = std::make_shared<const AnimationClip>(std::move(*retargeted));
    const std::scoped_lock lock(mutex);
    bindings.push_back({clip, skeleton, bound});
    return bound;
}

} // namespace Spot
