#include "registered_assets.h"
#include "temp_directory.h"
#include <spot/animation/animation_asset_format.h>
#include <spot/assets/asset_formats.h>

#include <spot.h>
#include <spot_import.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using namespace Spot;

namespace {

// A character's own skeleton, as its file has it: an armature above the hips, and a mesh beside it.
std::shared_ptr<const Skeleton> Character() {
    Skeleton skeleton;
    skeleton.name = "Character";
    skeleton.bones = {{"Armature", -1, {}},
                      {"mixamorig:Hips", 0, {{0, 1, 0}}},
                      {"mixamorig:Spine", 1, {{0, 0.2f, 0}}},
                      {"Body", -1, {}}};
    skeleton.joints = {1, 2};
    skeleton.inverseBindMatrices = {Matrix4{}, Matrix4{}};
    return std::make_shared<const Skeleton>(std::move(skeleton));
}

// An animation downloaded apart from the character, for a taller one, under another namespace and without the
// armature or the mesh.
AnimationClip Walk() {
    const auto turn = Quaternion::FromAxisAngle({1, 0, 0}, 0.5f);
    AnimationClip clip;
    clip.name = "Walk";
    clip.duration = 1;
    clip.rig = {{"mixamorig1:Hips", -1, {{0, 1.2f, 0}}}, {"mixamorig1:Spine", 0, {{0, 0.25f, 0}}}};
    clip.channels = {
        {0, AnimationPath::Translation, AnimationInterpolation::Linear, {0, 1}, {{0, 1.2f, 0, 0}, {0, 1.32f, 1.2f, 0}}},
        {1, AnimationPath::Translation, AnimationInterpolation::Linear, {0, 1}, {{0, 0.25f, 0, 0}, {0, 0.25f, 0, 0}}},
        {1,
         AnimationPath::Rotation,
         AnimationInterpolation::Linear,
         {0, 1},
         {{0, 0, 0, 1}, {turn.x, turn.y, turn.z, turn.w}}},
    };
    return clip;
}

} // namespace

TEST_CASE("Bone base names drop the namespaces tools give them", "[animation][retarget]") {
    CHECK(GetBoneBaseName("mixamorig:Hips") == "Hips");
    CHECK(GetBoneBaseName("mixamorig1:Hips") == "Hips");
    CHECK(GetBoneBaseName("Armature|Hips") == "Hips");
    CHECK(GetBoneBaseName("Hips") == "Hips");
}

TEST_CASE("Clips made for another rig play on a skeleton by bone names, in its proportions", "[animation][retarget]") {
    const auto skeleton = Character();
    const AnimationClip clip = Walk();
    CHECK_FALSE(IsClipForSkeleton(clip, *skeleton));

    const auto retargeted = RetargetAnimationClip(clip, *skeleton);
    REQUIRE(retargeted);
    CHECK(retargeted->rig.empty());
    REQUIRE(retargeted->channels.size() == 2);
    // The hips move as far for their height as the clip's did for theirs.
    const auto& hips = retargeted->channels[0];
    CHECK(hips.bone == 1);
    CHECK(hips.path == AnimationPath::Translation);
    CHECK(hips.values[1].y == Catch::Approx(1.1f));
    CHECK(hips.values[1].z == Catch::Approx(1.0f));
    // The spine keeps its own length, and only turns.
    CHECK(retargeted->channels[1].bone == 2);
    CHECK(retargeted->channels[1].path == AnimationPath::Rotation);

    auto kept = clip;
    kept.retargetTranslations = false;
    const auto unscaled = RetargetAnimationClip(kept, *skeleton);
    REQUIRE(unscaled);
    REQUIRE(unscaled->channels.size() == 3);
    CHECK(unscaled->channels[0].values[1].y == Catch::Approx(1.32f));

    Scene scene;
    auto& animator = scene.CreateEntity().AddComponent<Animator>();
    animator.SetSkeleton(skeleton);
    REQUIRE(animator.Play(std::make_shared<const AnimationClip>(clip)));
    animator.Seek(0.5f);
    const auto pose = animator.GetLocalPose();
    CHECK(pose[1].translation.y == Catch::Approx(1.05f));
    CHECK(pose[1].translation.z == Catch::Approx(0.5f));
    CHECK(pose[2].translation.y == Catch::Approx(0.2f));
    CHECK(pose[2].rotation.x > 0.1f);
}

TEST_CASE("Retargeted rotations turn from each rig's own rest pose", "[animation][retarget]") {
    const auto rest = Quaternion::FromAxisAngle({0, 0, 1}, 1.0f);
    AnimationClip clip;
    clip.duration = 1;
    clip.rig = {{"Arm", -1, {{}, rest}}};
    clip.channels = {
        {0, AnimationPath::Rotation, AnimationInterpolation::Linear, {0}, {{rest.x, rest.y, rest.z, rest.w}}}};
    const Skeleton skeleton{"Other", {{"arm", -1, {}}}, {0}, {Matrix4{}}};
    const auto retargeted = RetargetAnimationClip(clip, skeleton);
    REQUIRE(retargeted);
    REQUIRE(retargeted->channels.size() == 1);
    // At rest in the clip, so at rest on the skeleton.
    CHECK(std::abs(retargeted->channels[0].values[0].w) == Catch::Approx(1.0f));
}

TEST_CASE("Clips without a bone of the skeleton are rejected, and alike rigs share one bound copy",
          "[animation][retarget]") {
    const auto skeleton = Character();
    AnimationClip unrelated = Walk();
    for (auto& bone : unrelated.rig) {
        bone.name = "Tentacle";
    }
    CHECK_FALSE(RetargetAnimationClip(unrelated, *skeleton));
    Scene scene;
    auto& animator = scene.CreateEntity().AddComponent<Animator>();
    animator.SetSkeleton(skeleton);
    CHECK_FALSE(animator.Play(std::make_shared<const AnimationClip>(unrelated)));

    const auto clip = std::make_shared<const AnimationClip>(Walk());
    const auto first = BindAnimationClip(clip, skeleton);
    REQUIRE(first);
    CHECK(first != clip);
    CHECK(BindAnimationClip(clip, skeleton) == first);

    // A clip of the skeleton's own file plays as it is.
    auto own = Walk();
    own.rig.clear();
    for (const auto& bone : skeleton->bones) {
        own.rig.push_back(bone);
    }
    own.channels = {{1, AnimationPath::Translation, AnimationInterpolation::Linear, {0}, {{0, 1, 0, 0}}}};
    const auto ownClip = std::make_shared<const AnimationClip>(own);
    CHECK(IsClipForSkeleton(own, *skeleton));
    CHECK(BindAnimationClip(ownClip, skeleton) == ownClip);
}

TEST_CASE("Root motion moves along the ground and leaves the hips' rise and fall in the pose",
          "[animation][root_motion]") {
    const Skeleton skeleton{"Rig", {{"Hips", -1, {{0, 1, 0}}}}, {0}, {Matrix4{}}};
    const auto turn = Quaternion::FromAxisAngle({0, 1, 0}, 0.5f);
    AnimationClip clip;
    clip.duration = 1;
    clip.channels = {
        {0,
         AnimationPath::Translation,
         AnimationInterpolation::Linear,
         {0, 0.5f, 1},
         {{0, 1, 0, 0}, {0, 1.1f, 0.5f, 0}, {0, 1, 1, 0}}},
        {0,
         AnimationPath::Rotation,
         AnimationInterpolation::Linear,
         {0, 1},
         {{0, 0, 0, 1}, {turn.x, turn.y, turn.z, turn.w}}},
    };
    const auto delta = GetRootMotionDelta(skeleton, clip, 0, 0, 0.5f, false);
    CHECK(delta.translation.y == Catch::Approx(0).margin(1e-5f));
    CHECK(delta.translation.z == Catch::Approx(0.5f));
    CHECK(std::abs(delta.rotation.y) > 0.05f);

    std::vector<TransformComponents> pose(1);
    Pose::Sample(skeleton, clip, 0.5f, false, pose);
    RemoveRootMotion(skeleton, 0, true, pose[0]);
    CHECK(pose[0].translation.y == Catch::Approx(1.1f));
    CHECK(pose[0].translation.z == Catch::Approx(0).margin(1e-5f));
    CHECK(std::abs(pose[0].rotation.w) == Catch::Approx(1.0f));

    // Walks that sway their hips keep the turning in the pose.
    clip.rootMotionRotation = false;
    CHECK(std::abs(GetRootMotionDelta(skeleton, clip, 0, 0, 0.5f, false).rotation.w) == Catch::Approx(1.0f));
    Pose::Sample(skeleton, clip, 0.5f, false, pose);
    RemoveRootMotion(skeleton, 0, false, pose[0]);
    CHECK(std::abs(pose[0].rotation.y) > 0.05f);
}

TEST_CASE("Clips keep their rig and settings in their imported form", "[animation][retarget]") {
    auto clip = Walk();
    clip.retargetTranslations = false;
    clip.rootMotionRotation = false;
    Detail::BinaryWriter bytes;
    Detail::Encode(bytes, clip);
    const auto decoded = Detail::DecodeAnimationClip(bytes.GetData());
    REQUIRE(decoded);
    REQUIRE(decoded->rig.size() == 2);
    CHECK(decoded->rig[1].name == "mixamorig1:Spine");
    CHECK(decoded->rig[1].parent == 0);
    CHECK(decoded->rig[0].bindPose.translation.y == Catch::Approx(1.2f));
    CHECK_FALSE(decoded->retargetTranslations);
    CHECK_FALSE(decoded->rootMotionRotation);

    auto broken = clip;
    broken.channels[0].bone = 5;
    CHECK_FALSE(broken.IsValid());
}

TEST_CASE("Model clip settings rename, cut and split clips, which keep their file's rig", "[animation][assets]") {
    TempDirectory directory;
    const auto source = directory.GetPath() / "assets";
    std::filesystem::create_directories(source);
    std::filesystem::copy_file(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "models/rig.gltf", source / "rig.gltf");
    AssetDatabase database(source, directory.GetPath() / "imported");
    database.Refresh();
    const auto id = database.FindByPath("rig.gltf");
    REQUIRE(id);
    const AssetId whole = *database.FindByPath("rig.gltf#Move");
    ClipImportSettings first{.clip = "Move", .name = "Start", .end = 0.5f};
    ClipImportSettings second{.clip = "Move", .name = "Finish", .start = 0.5f, .rootMotionRotation = false};
    database.GetMeta(*id)->model.animations = {first, second};
    REQUIRE(database.SaveMeta(*id));
    REQUIRE(database.Reimport(*id));

    // The first part keeps the clip's id.
    CHECK(database.FindByPath("rig.gltf#Start") == whole);
    Spot::Tests::RegisteredAssetManager assets(directory.GetPath() / "imported");
    const auto start = assets.Load<AnimationClip>("rig.gltf#Start");
    const auto finish = assets.Load<AnimationClip>("rig.gltf#Finish");
    REQUIRE(start);
    REQUIRE(finish);
    CHECK(start->duration == Catch::Approx(0.5f));
    CHECK(finish->duration == Catch::Approx(0.5f));
    CHECK_FALSE(finish->rootMotionRotation);
    REQUIRE(start->rig.size() == 3);
    CHECK(start->rig[1].name == "Hand");
    for (const auto& channel : finish->channels) {
        CHECK(channel.times.front() == 0.0f);
        CHECK(channel.times.back() == Catch::Approx(0.5f));
    }

    // Skeletons of the same file play their clips as they are.
    const auto skeleton = assets.Load<Skeleton>("rig.gltf#Rig");
    REQUIRE(skeleton);
    CHECK(IsClipForSkeleton(*start, *skeleton));
}
