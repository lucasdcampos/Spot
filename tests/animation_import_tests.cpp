#include "registered_assets.h"
#include "temp_directory.h"
#include <spot/animation/animation_asset_format.h>
#include <spot/assets/asset_formats.h>

#include <spot.h>
#include <spot_import.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace Spot;

TEST_CASE("glTF imports skin streams, joint hierarchy and all channel interpolations", "[animation][model]") {
    const auto model = ModelData::Load(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "models/rig.gltf");
    REQUIRE(model);
    REQUIRE(model->skeletons.size() == 1);
    const auto& skeleton = model->skeletons[0];
    REQUIRE(skeleton.IsValid());
    REQUIRE(skeleton.bones.size() == 3);
    CHECK(skeleton.bones[1].parent == 0);
    CHECK(skeleton.joints == std::vector<std::uint32_t>{0, 1});
    CHECK(skeleton.FindBone("Hand") == 1);
    CHECK(skeleton.inverseBindMatrices[1].columns[3].y == -1);
    CHECK(model->nodes[0].joint);
    CHECK(model->nodes[1].joint);
    CHECK(model->nodes[2].skin == 0);
    REQUIRE(model->meshes[0].mesh.skin.size() == model->meshes[0].mesh.vertices.size());
    CHECK(model->meshes[0].mesh.skin[0].joints[1] == 1);
    CHECK(model->meshes[0].mesh.skin[0].weights.y == Catch::Approx(0.75f));
    REQUIRE(model->animations.size() == 1);
    const auto& clip = model->animations[0];
    CHECK(clip.IsValid());
    CHECK(clip.duration == 1);
    REQUIRE(clip.channels.size() == 3);
    CHECK(clip.channels[0].interpolation == AnimationInterpolation::Linear);
    CHECK(clip.channels[1].interpolation == AnimationInterpolation::Step);
    CHECK(clip.channels[2].interpolation == AnimationInterpolation::Cubic);
    CHECK(clip.channels[2].values.size() == 6);
    CHECK(clip.channels[0].bone == 1);
}

TEST_CASE("Animation assets cook, load without a GPU and keep subasset identities", "[animation][assets]") {
    const TempDirectory directory;
    const auto source = directory.GetPath() / "assets";
    std::filesystem::create_directories(source);
    for (const auto* file : {"rig.gltf", "animation_only.gltf"}) {
        std::filesystem::copy_file(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "models" / file, source / file);
    }
    AssetDatabase database(source, directory.GetPath() / "imported");
    database.Refresh();
    Spot::Tests::RegisteredAssetManager assets(directory.GetPath() / "imported");
    auto skeleton = assets.Load<Skeleton>("rig.gltf#Rig");
    auto clip = assets.Load<AnimationClip>("rig.gltf#Move");
    auto separate = assets.Load<AnimationClip>("animation_only.gltf#Move");
    REQUIRE(skeleton);
    REQUIRE(clip);
    REQUIRE(separate);
    CHECK(clip->channels.size() == 3);
    CHECK(separate->channels.size() == 3);
    const auto identity = clip.GetId();
    database.Refresh();
    assets.RefreshManifest();
    CHECK(assets.FindByPath("rig.gltf#Move") == identity);
    const auto model = assets.Load<Model>("rig.gltf");
    REQUIRE(model);
    CHECK(model->GetNodes()[2].skeleton.GetId() == skeleton.GetId());
    CHECK(model->GetNodes()[2].animations[0].GetId() == identity);
}

TEST_CASE("Animation formats reject truncation, invalid hierarchies and nonmonotonic keys", "[animation]") {
    const auto model = ModelData::Load(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "models/rig.gltf");
    REQUIRE(model);
    Detail::BinaryWriter skeletonBytes;
    Detail::Encode(skeletonBytes, model->skeletons[0]);
    REQUIRE(Detail::DecodeSkeleton(skeletonBytes.GetData()));
    for (std::size_t i = 0; i < skeletonBytes.GetData().size(); ++i) {
        CHECK_FALSE(Detail::DecodeSkeleton(skeletonBytes.GetData().first(i)));
    }
    Detail::BinaryWriter clipBytes;
    Detail::Encode(clipBytes, model->animations[0]);
    REQUIRE(Detail::DecodeAnimationClip(clipBytes.GetData()));
    for (std::size_t i = 0; i < clipBytes.GetData().size(); ++i) {
        CHECK_FALSE(Detail::DecodeAnimationClip(clipBytes.GetData().first(i)));
    }
    auto skeleton = model->skeletons[0];
    skeleton.bones[1].parent = 2;
    CHECK_FALSE(skeleton.IsValid());
    auto clip = model->animations[0];
    clip.channels[0].times[1] = 0;
    CHECK_FALSE(clip.IsValid());
    auto mesh = model->meshes[0].mesh;
    const auto copy = mesh;
    mesh.AppendSubmesh(copy);
    CHECK(mesh.skin.size() == mesh.vertices.size());
    Detail::BinaryWriter meshBytes;
    Detail::Encode(meshBytes, mesh);
    REQUIRE(Detail::DecodeMesh(meshBytes.GetData()));
    CHECK(Detail::DecodeMesh(meshBytes.GetData())->skin.size() == mesh.skin.size());
}
