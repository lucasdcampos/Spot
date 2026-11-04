#pragma once

#include "spot/animation/animation_data.h"

#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace Spot {

// A bone's name without the namespace tools put before it, as "Hips" for "mixamorig:Hips", "mixamorig1:Hips" or
// "Armature|Hips".
std::string_view GetBoneBaseName(std::string_view name);

// The parts of a human body, which rigs name in their own ways.
enum class HumanBone {
    Hips,
    Spine,
    Chest,
    Neck,
    Head,
    LeftShoulder,
    LeftUpperArm,
    LeftLowerArm,
    LeftHand,
    RightShoulder,
    RightUpperArm,
    RightLowerArm,
    RightHand,
    LeftUpperLeg,
    LeftLowerLeg,
    LeftFoot,
    LeftToes,
    RightUpperLeg,
    RightLowerLeg,
    RightFoot,
    RightToes,
    Count
};

// The bone that is this part of the body, by the names of the conventions known, as "LeftUpperArm" or Mixamo's
// "LeftArm", ignoring namespaces and case. Nothing when the rig has no bone of those names.
std::optional<std::size_t> FindHumanBone(std::span<const SkeletonBone> bones, HumanBone part);

// Whether the clip's channels address the skeleton's bones as they are: clips without a rig of their own, as made in
// code, and clips made for a rig with the same bones, hierarchy and lengths, as those of the skeleton's own file.
bool IsClipForSkeleton(const AnimationClip& clip, const Skeleton& skeleton);

// The clip rewritten for a skeleton other than the one it was made for, such as a character's animations downloaded
// apart from it. Bones match by name, exactly or else by their base names (GetBoneBaseName()) ignoring case. Rotations
// carry over relative to each rig's bind pose. With AnimationClip::retargetTranslations, only the topmost bones that
// move keep their translation, scaled by how far each rig places them from their parent, and every other bone keeps
// the skeleton's length. The result's channels address the skeleton's bones, and it has no rig. Nothing when no bone
// matches; the clip as it is when it is for the skeleton already.
//
// Human rigs named differently match by their parts (FindHumanBone()) and may rest in other poses and face other ways:
// each part turns in the model's space as it does in the clip, from a rest pose whose arms and legs lie as the clip's
// do, and the hips move for the height of the legs. Other bones, such as fingers, and scale keep the skeleton's.
std::optional<AnimationClip> RetargetAnimationClip(const AnimationClip& clip, const Skeleton& skeleton);

// RetargetAnimationClip() shared by everything that plays the clip on the skeleton, so characters alike keep one copy:
// the clip itself when it is for the skeleton, a retargeted copy kept while something holds it, or null when no bone
// matches. Any thread may call it.
std::shared_ptr<const AnimationClip> BindAnimationClip(const std::shared_ptr<const AnimationClip>& clip,
                                                       const std::shared_ptr<const Skeleton>& skeleton);

} // namespace Spot
