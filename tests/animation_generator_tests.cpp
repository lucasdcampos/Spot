#include "temp_directory.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <generator.h>
#include <rigs.h>
#include <spot.h>
#include <trees.h>

#include <cmath>

// Only the rig is examined, so painting smaller textures keeps generation quick.
TEST_CASE("The generated humanoid has articulated limbs, valid skin and animation clips", "[animation][generator]") {
    TempDirectory directory;
    const auto first = Generator::GenerateSandboxAssets(directory.GetPath(), 8);
    REQUIRE(first.failed == 0);
    for (const auto name : Generator::GetRigModelNames()) {
        CAPTURE(name);
        const auto data = Spot::ModelData::Load(directory.GetPath() / "models" / (std::string(name) + ".gltf"));
        REQUIRE(data);
        REQUIRE(data->skeletons.size() == 1);
        const auto& skeleton = data->skeletons[0];
        REQUIRE(skeleton.IsValid());
        CHECK(skeleton.joints.size() == 21);
        for (const auto side : {"Left", "Right"}) {
            for (const auto limb : {"UpperArm", "Forearm", "Hand", "UpperLeg", "LowerLeg", "Foot", "Toes"}) {
                REQUIRE(skeleton.FindBone(std::string(side) + limb));
            }
            const auto upperArm = *skeleton.FindBone(std::string(side) + "UpperArm");
            const auto forearm = *skeleton.FindBone(std::string(side) + "Forearm");
            const auto hand = *skeleton.FindBone(std::string(side) + "Hand");
            CHECK(skeleton.bones[forearm].parent == static_cast<std::int32_t>(upperArm));
            CHECK(skeleton.bones[hand].parent == static_cast<std::int32_t>(forearm));
            const auto thigh = *skeleton.FindBone(std::string(side) + "UpperLeg");
            const auto shin = *skeleton.FindBone(std::string(side) + "LowerLeg");
            CHECK(skeleton.bones[shin].parent == static_cast<std::int32_t>(thigh));
        }
        std::vector<Spot::TransformComponents> local(skeleton.bones.size());
        std::vector<Spot::Matrix4> model(skeleton.bones.size()), palette(skeleton.joints.size());
        Spot::Pose::Bind(skeleton, local);
        Spot::Pose::ToModel(skeleton, local, model);
        Spot::Pose::Palette(skeleton, model, {}, palette);
        for (const auto& matrix : palette) {
            for (std::size_t column = 0; column < 4; ++column) {
                const auto expected = Spot::Matrix4{}.columns[column], actual = matrix.columns[column];
                CHECK(std::abs(actual.x - expected.x) < 1e-4f);
                CHECK(std::abs(actual.y - expected.y) < 1e-4f);
                CHECK(std::abs(actual.z - expected.z) < 1e-4f);
                CHECK(std::abs(actual.w - expected.w) < 1e-4f);
            }
        }
        for (const auto& mesh : data->meshes) {
            CHECK(mesh.mesh.skin.size() == mesh.mesh.vertices.size());
            for (const auto& vertex : mesh.mesh.skin) {
                CHECK(vertex.joints[0] < skeleton.joints.size());
            }
        }
        CHECK(data->animations.size() == 21);
        for (const auto& clip : data->animations) {
            REQUIRE(clip.IsValid());
            Spot::Pose::Sample(skeleton, clip, clip.duration * 0.5f, false, local);
            Spot::Pose::ToModel(skeleton, local, model);
            CHECK(model.size() == skeleton.bones.size());
        }
        const auto root = skeleton.FindBone("Pelvis");
        REQUIRE(root);
        CHECK(Spot::GetRootMotionDelta(skeleton, data->animations[1], *root, 0, 1, false).translation.z ==
              Catch::Approx(-0.85f));
        CHECK(data->animations[1].events.size() == 2);
        CHECK(data->animations[2].events.size() == 2);
        Spot::Pose::Sample(skeleton, data->animations[2], 0.15f, false, local);
        CHECK(std::abs(local[*skeleton.FindBone("LeftLowerLeg")].rotation.x) > 0.1f);
        Spot::Pose::Sample(skeleton, data->animations[3], 0, false, local);
        CHECK(std::abs(local[*skeleton.FindBone("RightForearm")].rotation.x) > 0.1f);
        for (const std::size_t index : {4u, 5u}) {
            CHECK(Spot::Length(
                      Spot::GetRootMotionDelta(skeleton, data->animations[index], *root, 0, 1, false).translation) <
                  1e-5f);
        }
        Spot::Pose::Sample(skeleton, data->animations[6], 0, false, local);
        const auto hip = *skeleton.FindBone("LeftUpperLeg");
        const auto knee = *skeleton.FindBone("LeftLowerLeg");
        CHECK((local[hip].rotation * Spot::Vector3{0, -1, 0}).z < -0.99f);
        CHECK((local[hip].rotation * local[knee].rotation * Spot::Vector3{0, -1, 0}).y < -0.99f);
    }
    const auto second = Generator::GenerateSandboxAssets(directory.GetPath(), 8);
    CHECK(second.failed == 0);
    CHECK(second.written == 0);
    CHECK(second.unchanged == first.written);
}

TEST_CASE("Pine branches and needles have valid independent joints", "[animation][generator][wind]") {
    for (const bool sparse : {false, true}) {
        const auto tree = Generator::Trees::Pine(7301, sparse);
        REQUIRE(tree.skeleton.IsValid());
        REQUIRE(tree.skeleton.joints.size() > 100);
        for (const auto* mesh : {&tree.bark, &tree.foliage}) {
            REQUIRE(mesh->skin.size() == mesh->vertices.size());
            for (const auto& vertex : mesh->skin) {
                REQUIRE(vertex.joints[0] < tree.skeleton.joints.size());
            }
        }
        std::vector<Spot::TransformComponents> local(tree.skeleton.bones.size());
        std::vector<Spot::Matrix4> model(local.size()), palette(tree.skeleton.joints.size());
        Spot::Pose::Bind(tree.skeleton, local);
        Spot::Pose::ToModel(tree.skeleton, local, model);
        Spot::Pose::Palette(tree.skeleton, model, {}, palette);
        for (const auto& matrix : palette) {
            const auto point = matrix * Spot::Vector4{1, 2, 3, 1};
            CHECK(point.x == Catch::Approx(1).margin(0.0001f));
            CHECK(point.y == Catch::Approx(2).margin(0.0001f));
            CHECK(point.z == Catch::Approx(3).margin(0.0001f));
        }
    }
}

TEST_CASE("Humanoid locomotion loops keep planted feet grounded and crouching knees bent",
          "[animation][generator][locomotion]") {
    const auto rig = Generator::Rigs::Humanoid();
    const auto& skeleton = rig.skeleton;
    std::vector<Spot::TransformComponents> local(skeleton.bones.size());
    std::vector<Spot::Matrix4> model(skeleton.bones.size());
    for (const auto& clip : rig.animations) {
        if (clip.name == "Idle" || clip.name == "HoldPistol" || clip.name.starts_with("Seated") ||
            clip.name.starts_with("Jump")) {
            continue;
        }
        CAPTURE(clip.name);
        REQUIRE(clip.IsValid());
        for (int sample = 0; sample < 32; ++sample) {
            const float phase = static_cast<float>(sample) / 32;
            Spot::Pose::Sample(skeleton, clip, phase * clip.duration, false, local);
            if (clip.name == "Walk" || clip.name == "Run") {
                local[0].translation.z = 0;
            }
            Spot::Pose::ToModel(skeleton, local, model);
            for (const bool left : {true, false}) {
                const auto foot = *skeleton.FindBone(left ? "LeftFoot" : "RightFoot");
                const auto point = model[foot] * Spot::Vector4{0, 0, 0, 1};
                const float footPhase = std::fmod(phase + (left ? 0.0f : 0.5f), 1.0f);
                CHECK(point.y >= -0.871f);
                if (footPhase < 0.6f || clip.name == "Crouch") {
                    CHECK(point.y == Catch::Approx(-0.87f).margin(0.002f));
                }
                if (clip.name.starts_with("Crouch")) {
                    CHECK(local[0].translation.y < -0.27f);
                    const auto knee = *skeleton.FindBone(left ? "LeftLowerLeg" : "RightLowerLeg");
                    CHECK(std::abs(local[knee].rotation.x) > 0.4f);
                }
            }
        }
        std::vector<Spot::TransformComponents> end(local.size());
        Spot::Pose::Sample(skeleton, clip, 0, false, local);
        Spot::Pose::Sample(skeleton, clip, clip.duration, false, end);
        for (std::size_t bone = 1; bone < local.size(); ++bone) {
            CHECK(Spot::Length(local[bone].rotation * Spot::Vector3{0, -1, 0} -
                               end[bone].rotation * Spot::Vector3{0, -1, 0}) < 0.001f);
        }
    }
}

TEST_CASE("Jump poses keep bent legs off the ground without driving physics", "[animation][generator][jump]") {
    const auto rig = Generator::Rigs::Humanoid();
    std::vector<Spot::TransformComponents> local(rig.skeleton.bones.size());
    std::vector<Spot::Matrix4> model(local.size());
    for (const auto& clip : rig.animations) {
        if (!clip.name.starts_with("Jump")) {
            continue;
        }
        CAPTURE(clip.name);
        REQUIRE(clip.IsValid());
        CHECK(clip.events.empty());
        Spot::Pose::Sample(rig.skeleton, clip, 0.25f, false, local);
        Spot::Pose::ToModel(rig.skeleton, local, model);
        CHECK(Spot::Length(Spot::GetRootMotionDelta(rig.skeleton, clip, 0, 0, 1, false).translation) < 0.0001f);
        for (const auto* name : {"LeftFoot", "RightFoot"}) {
            const auto bone = *rig.skeleton.FindBone(name);
            CHECK((model[bone] * Spot::Vector4{0, 0, 0, 1}).y > -0.85f);
        }
    }
}
