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

// A body facing -Z with its arms hanging, under the common names, with its hips at the model's origin.
std::vector<SkeletonBone> HangingBody() {
    return {{"Pelvis", -1, {}},
            {"Spine", 0, {{0, 0.16f, 0}}},
            {"Chest", 1, {{0, 0.2f, 0}}},
            {"Neck", 2, {{0, 0.2f, 0}}},
            {"Head", 3, {{0, 0.1f, 0}}},
            {"LeftShoulder", 2, {{-0.17f, 0.1f, 0}}},
            {"LeftUpperArm", 5, {{-0.06f, 0, 0}}},
            {"LeftForearm", 6, {{0, -0.29f, 0}}},
            {"LeftHand", 7, {{0, -0.26f, 0}}},
            {"RightShoulder", 2, {{0.17f, 0.1f, 0}}},
            {"RightUpperArm", 9, {{0.06f, 0, 0}}},
            {"RightForearm", 10, {{0, -0.29f, 0}}},
            {"RightHand", 11, {{0, -0.26f, 0}}},
            {"LeftUpperLeg", 0, {{-0.1f, -0.07f, 0}}},
            {"LeftLowerLeg", 13, {{0, -0.41f, 0}}},
            {"LeftFoot", 14, {{0, -0.39f, 0}}},
            {"RightUpperLeg", 0, {{0.1f, -0.07f, 0}}},
            {"RightLowerLeg", 16, {{0, -0.41f, 0}}},
            {"RightFoot", 17, {{0, -0.39f, 0}}}};
}

// A Mixamo body in T-pose, facing +Z with its feet at the origin, whose bones do not lie along their parents' axes,
// with a spine bone and fingers the clip does not have.
std::shared_ptr<const Skeleton> MixamoBody() {
    const auto roll = Quaternion::FromAxisAngle({0, 0, 1}, 0.7f);
    Skeleton skeleton;
    skeleton.name = "Mixamo";
    skeleton.bones = {{"mixamorig:Hips", -1, {{0, 1, 0}}},
                      {"mixamorig:Spine", 0, {{0, 0.1f, 0}}},
                      {"mixamorig:Spine1", 1, {{0, 0.12f, 0}}},
                      {"mixamorig:Spine2", 2, {{0, 0.14f, 0}}},
                      {"mixamorig:Neck", 3, {{0, 0.15f, 0}}},
                      {"mixamorig:Head", 4, {{0, 0.07f, 0}}},
                      {"mixamorig:LeftShoulder", 3, {{0.07f, 0.13f, 0}}},
                      {"mixamorig:LeftArm", 6, {{0.13f, 0, 0}, roll}},
                      {"mixamorig:LeftForeArm", 7, {Inverse(roll) * Vector3{0.25f, 0, 0}, Inverse(roll)}},
                      {"mixamorig:LeftHand", 8, {{0.25f, 0, 0}}},
                      {"mixamorig:LeftHandIndex1", 9, {{0.1f, 0, 0}}},
                      {"mixamorig:RightShoulder", 3, {{-0.07f, 0.13f, 0}}},
                      {"mixamorig:RightArm", 11, {{-0.13f, 0, 0}}},
                      {"mixamorig:RightForeArm", 12, {{-0.25f, 0, 0}}},
                      {"mixamorig:RightHand", 13, {{-0.25f, 0, 0}}},
                      {"mixamorig:LeftUpLeg", 0, {{0.09f, -0.06f, 0}}},
                      {"mixamorig:LeftLeg", 15, {{0, -0.42f, 0}}},
                      {"mixamorig:LeftFoot", 16, {{0, -0.38f, 0}}},
                      {"mixamorig:RightUpLeg", 0, {{-0.09f, -0.06f, 0}}},
                      {"mixamorig:RightLeg", 18, {{0, -0.42f, 0}}},
                      {"mixamorig:RightFoot", 19, {{0, -0.38f, 0}}}};
    skeleton.joints = {0};
    skeleton.inverseBindMatrices = {Matrix4{}};
    return std::make_shared<const Skeleton>(std::move(skeleton));
}

Vector3 SampleDirection(const Skeleton& skeleton, const AnimationClip& clip, float time, std::string_view from,
                        std::string_view to) {
    std::vector<TransformComponents> local(skeleton.bones.size());
    std::vector<Matrix4> model(skeleton.bones.size());
    Pose::Sample(skeleton, clip, time, false, local);
    Pose::ToModel(skeleton, local, model);
    const Vector3 start = Decompose(model[*skeleton.FindBone(from)]).translation;
    const Vector3 end = Decompose(model[*skeleton.FindBone(to)]).translation;
    return Normalize(end - start);
}

void CheckDirection(Vector3 actual, Vector3 expected) {
    CHECK(actual.x == Catch::Approx(expected.x).margin(1e-3f));
    CHECK(actual.y == Catch::Approx(expected.y).margin(1e-3f));
    CHECK(actual.z == Catch::Approx(expected.z).margin(1e-3f));
}

} // namespace

TEST_CASE("Human bones are found by the names of each convention", "[animation][retarget]") {
    const auto common = HangingBody();
    REQUIRE(FindHumanBone(common, HumanBone::Hips));
    CHECK(common[*FindHumanBone(common, HumanBone::Hips)].name == "Pelvis");
    CHECK(common[*FindHumanBone(common, HumanBone::LeftLowerArm)].name == "LeftForearm");

    const auto mixamo = MixamoBody();
    CHECK(mixamo->bones[*FindHumanBone(mixamo->bones, HumanBone::Hips)].name == "mixamorig:Hips");
    CHECK(mixamo->bones[*FindHumanBone(mixamo->bones, HumanBone::Chest)].name == "mixamorig:Spine2");
    CHECK(mixamo->bones[*FindHumanBone(mixamo->bones, HumanBone::RightUpperLeg)].name == "mixamorig:RightUpLeg");
    CHECK_FALSE(FindHumanBone(mixamo->bones, HumanBone::LeftToes));
}

TEST_CASE("Clips of a human rig play on one named otherwise that rests and faces another way",
          "[animation][retarget]") {
    const auto skeleton = MixamoBody();
    const auto swing = Quaternion::FromAxisAngle({1, 0, 0}, 0.6f);
    AnimationClip clip;
    clip.name = "Walk";
    clip.duration = 1;
    clip.rig = HangingBody();
    clip.events = {{0.5f, "Footstep", "left"}};
    // Swings the arm toward the way the body faces, bends the elbow, and lowers and moves the hips forward.
    clip.channels = {
        {0, AnimationPath::Translation, AnimationInterpolation::Linear, {0, 1}, {{0, 0, 0, 0}, {0, -0.087f, -0.2f, 0}}},
        {6, AnimationPath::Rotation, AnimationInterpolation::Linear, {0, 1}, {{0, 0, 0, 1}, {swing.x, swing.y, swing.z, swing.w}}},
    };
    CHECK_FALSE(IsClipForSkeleton(clip, *skeleton));

    const auto retargeted = RetargetAnimationClip(clip, *skeleton);
    REQUIRE(retargeted);
    REQUIRE(retargeted->IsValid());
    CHECK(retargeted->rig.empty());
    CHECK(retargeted->name == "Walk");
    REQUIRE(retargeted->events.size() == 1);

    // At the clip's rest, the T-pose's arms hang as the clip's do, and its legs stay straight down.
    CheckDirection(SampleDirection(*skeleton, *retargeted, 0, "mixamorig:LeftArm", "mixamorig:LeftForeArm"), {0, -1, 0});
    CheckDirection(SampleDirection(*skeleton, *retargeted, 0, "mixamorig:LeftForeArm", "mixamorig:LeftHand"), {0, -1, 0});
    CheckDirection(SampleDirection(*skeleton, *retargeted, 0, "mixamorig:RightArm", "mixamorig:RightForeArm"), {0, -1, 0});
    CheckDirection(SampleDirection(*skeleton, *retargeted, 0, "mixamorig:LeftUpLeg", "mixamorig:LeftLeg"), {0, -1, 0});
    // Fingers follow the hand.
    CheckDirection(SampleDirection(*skeleton, *retargeted, 0, "mixamorig:LeftHand", "mixamorig:LeftHandIndex1"), {0, -1, 0});

    // The swing toward the clip's -Z goes toward the +Z this body faces, and the other arm stays.
    const Vector3 down{0, -std::cos(0.6f), std::sin(0.6f)};
    CheckDirection(SampleDirection(*skeleton, *retargeted, 1, "mixamorig:LeftArm", "mixamorig:LeftForeArm"), down);
    CheckDirection(SampleDirection(*skeleton, *retargeted, 1, "mixamorig:RightArm", "mixamorig:RightForeArm"), {0, -1, 0});

    // The hips keep their height, lowered for these legs: 0.86 for the clip's 0.87.
    std::vector<TransformComponents> pose(skeleton->bones.size());
    Pose::Sample(*skeleton, *retargeted, 1, false, pose);
    CHECK(pose[0].translation.y == Catch::Approx(1.0f - 0.087f * 0.86f / 0.87f));
    CHECK(pose[0].translation.z == Catch::Approx(0.2f * 0.86f / 0.87f));

    // Clips without movement of the hips leave them where the skeleton has them.
    auto still = clip;
    still.channels.erase(still.channels.begin());
    const auto stillBody = RetargetAnimationClip(still, *skeleton);
    REQUIRE(stillBody);
    Pose::Sample(*skeleton, *stillBody, 1, false, pose);
    CHECK(pose[0].translation.y == Catch::Approx(1.0f));
}

TEST_CASE("A mesh with a skin of its own takes its palette from it", "[animation][retarget]") {
    const auto shifted = Matrix4::Translation({0, 0, 3});
    Skeleton own = *MixamoBody();
    own.joints = {2, 1};
    own.inverseBindMatrices = {shifted, Matrix4{}};
    Scene scene;
    auto& root = scene.CreateEntity();
    auto& animator = root.AddComponent<Animator>();
    animator.SetSkeleton(MixamoBody());
    auto& renderer = root.AddComponent<SkinnedMeshRenderer>();
    renderer.SetAnimator(root.GetId());
    animator.Seek(0);
    animator.RefreshRenderers();
    REQUIRE(renderer.GetPalette().size() == 1);
    renderer.SetSkinSkeleton(std::make_shared<const Skeleton>(own));
    animator.RefreshRenderers();
    REQUIRE(renderer.GetPalette().size() == 2);
    const auto point = renderer.GetPalette()[0] * Vector4{0, 0, 0, 1};
    CHECK(point.z == Catch::Approx(3.0f));
    CHECK(point.y == Catch::Approx(1.22f));
}

TEST_CASE("Human rigs named alike keep sharing clips by name", "[animation][retarget]") {
    auto lower = std::make_shared<Skeleton>(*MixamoBody());
    lower->bones[0].bindPose.translation.y = 0.9f;
    AnimationClip clip;
    clip.duration = 1;
    for (const auto& bone : MixamoBody()->bones) {
        clip.rig.push_back(bone);
    }
    clip.channels = {{0, AnimationPath::Rotation, AnimationInterpolation::Linear, {0}, {{0, 0, 0, 1}}}};
    const auto retargeted = RetargetAnimationClip(clip, *lower);
    REQUIRE(retargeted);
    // Only what the clip has, unlike a clip carried over part by part.
    CHECK(retargeted->channels.size() == 1);
}

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
