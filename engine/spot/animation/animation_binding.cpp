#include "spot/animation/animation_binding.h"

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

} // namespace

std::string_view GetBoneBaseName(std::string_view name) {
    const auto separator = name.find_last_of(":|");
    return separator == std::string_view::npos ? name : name.substr(separator + 1);
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
