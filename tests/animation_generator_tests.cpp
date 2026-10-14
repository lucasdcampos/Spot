#include "temp_directory.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <generator.h>
#include <spot.h>

#include <cmath>

TEST_CASE("The generated robot has a valid skin, bind palettes and animation clips", "[animation][generator]") {
    TempDirectory directory;
    const auto first = Generator::GenerateSandboxAssets(directory.GetPath());
    REQUIRE(first.failed == 0);
    for (const auto name : Generator::GetRigModelNames()) {
        CAPTURE(name);
        const auto data = Spot::ModelData::Load(directory.GetPath() / "models" / (std::string(name) + ".gltf"));
        REQUIRE(data);
        REQUIRE(data->skeletons.size() == 1);
        const auto& skeleton = data->skeletons[0];
        REQUIRE(skeleton.IsValid());
        CHECK(skeleton.joints.size() == 11);
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
        CHECK(data->animations.size() == 3);
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
    }
    const auto second = Generator::GenerateSandboxAssets(directory.GetPath());
    CHECK(second.failed == 0);
    CHECK(second.written == 0);
    CHECK(second.unchanged == first.written);
}
