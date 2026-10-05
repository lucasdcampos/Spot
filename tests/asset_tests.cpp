#include "temp_directory.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <unordered_set>

using Spot::AssetDatabase;
using Spot::AssetId;
using Spot::AssetMeta;
using Spot::AssetType;

namespace {

const std::filesystem::path TestAssets = SPOT_TEST_ASSETS_DIR;

void CopyFile(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::filesystem::create_directories(to.parent_path());
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
}

} // namespace

TEST_CASE("Asset ids are random and round-trip through text", "[assets]") {
    std::unordered_set<AssetId> ids;
    for (int i = 0; i < 100; ++i) {
        const AssetId id = AssetId::Generate();
        CHECK(id.IsValid());
        ids.insert(id);
        CHECK(AssetId::Parse(id.ToString()) == id);
    }
    CHECK(ids.size() == 100);

    CHECK_FALSE(AssetId{}.IsValid());
    const auto parsed = AssetId::Parse("0123456789ABCDEF0123456789abcdef");
    REQUIRE(parsed.has_value());
    CHECK(parsed->high == 0x0123456789abcdefull);
    CHECK(parsed->ToString() == "0123456789abcdef0123456789abcdef");
    CHECK_FALSE(AssetId::Parse("0123").has_value());
    CHECK_FALSE(AssetId::Parse("0123456789abcdef0123456789abcdeg").has_value());
}

TEST_CASE("Asset types come from file extensions", "[assets]") {
    CHECK(Spot::GetSourceAssetType("a/b/Image.PNG") == AssetType::Texture);
    CHECK(Spot::GetSourceAssetType("house.glb") == AssetType::Model);
    CHECK(Spot::GetSourceAssetType("tone.ogg") == AssetType::Sound);
    CHECK(Spot::GetSourceAssetType("font.ttf") == AssetType::Font);
    CHECK(Spot::GetSourceAssetType("red.material") == AssetType::Material);
    CHECK(Spot::GetSourceAssetType("level.scene") == AssetType::Scene);
    CHECK_FALSE(Spot::GetSourceAssetType("house.glb.meta").has_value());
    CHECK_FALSE(Spot::GetSourceAssetType("buffer.bin").has_value());

    CHECK(Spot::ParseAssetType(Spot::GetAssetTypeName(AssetType::Mesh)) == AssetType::Mesh);
    CHECK_FALSE(Spot::ParseAssetType("Banana").has_value());
}

TEST_CASE("Meta files keep the id, the settings of their type and sub-asset ids", "[assets]") {
    AssetMeta meta;
    meta.id = AssetId::Generate();
    meta.type = AssetType::Model;
    meta.model.scale = 0.01f;
    const AssetId meshId = meta.GetOrAddSubAssetId("mesh:0");
    CHECK(meta.GetOrAddSubAssetId("mesh:0") == meshId);
    CHECK(meta.GetOrAddSubAssetId("mesh:1") != meshId);

    const auto loaded = AssetMeta::LoadFromString(meta.SaveToString());
    REQUIRE(loaded.has_value());
    CHECK(loaded->id == meta.id);
    CHECK(loaded->type == AssetType::Model);
    CHECK(loaded->model.scale == 0.01f);
    REQUIRE(loaded->subAssets.size() == 2);
    CHECK(loaded->subAssets[0].key == "mesh:0");
    CHECK(loaded->subAssets[0].id == meshId);

    // Only the settings of the asset's own type matter.
    AssetMeta changed = meta;
    changed.texture.mipmaps = !changed.texture.mipmaps;
    CHECK(changed.GetSettingsString() == meta.GetSettingsString());
    changed.model.scale = 2.0f;
    CHECK(changed.GetSettingsString() != meta.GetSettingsString());

    CHECK_FALSE(AssetMeta::LoadFromString("{}").has_value());
    CHECK_FALSE(AssetMeta::LoadFromString("not json").has_value());
}

TEST_CASE("The asset database gives source files meta files with lasting ids", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    CopyFile(TestAssets / "pixels.png", sources / "pixels.png");
    CopyFile(TestAssets / "models" / "cube.glb", sources / "models" / "cube.glb");
    CopyFile(TestAssets / "fonts" / "OFL.txt", sources / "fonts" / "OFL.txt");
    CopyFile(TestAssets / "pixels.png", sources / ".hidden" / "skipped.png");

    AssetDatabase database(sources, project.GetPath() / "imported");
    database.Refresh();

    REQUIRE(database.GetAssets().size() == 2);
    CHECK(database.GetAssets()[0].path == "models/cube.glb");
    CHECK(database.GetAssets()[0].type == AssetType::Model);
    CHECK(database.GetAssets()[1].path == "pixels.png");
    CHECK(std::filesystem::exists(sources / "pixels.png.meta"));
    CHECK_FALSE(std::filesystem::exists(sources / "fonts" / "OFL.txt.meta"));
    CHECK_FALSE(std::filesystem::exists(sources / ".hidden" / "skipped.png.meta"));

    const auto pixelsId = database.FindByPath("pixels.png");
    REQUIRE(pixelsId.has_value());
    CHECK(database.Find(*pixelsId)->path == "pixels.png");
    REQUIRE(database.GetMeta(*pixelsId) != nullptr);
    CHECK(database.GetMeta(*pixelsId)->type == AssetType::Texture);

    SECTION("Ids survive a new database and a move") {
        std::filesystem::create_directories(sources / "textures");
        std::filesystem::rename(sources / "pixels.png", sources / "textures" / "moved.png");
        std::filesystem::rename(sources / "pixels.png.meta", sources / "textures" / "moved.png.meta");

        AssetDatabase reopened(sources, project.GetPath() / "imported");
        reopened.Refresh();
        CHECK(reopened.FindByPath("textures/moved.png") == pixelsId);
    }

    SECTION("Copies of a file with its meta file get a new id") {
        CopyFile(sources / "pixels.png", sources / "pixels_copy.png");
        CopyFile(sources / "pixels.png.meta", sources / "pixels_copy.png.meta");
        database.Refresh();

        CHECK(database.FindByPath("pixels.png") == pixelsId);
        const auto copyId = database.FindByPath("pixels_copy.png");
        REQUIRE(copyId.has_value());
        CHECK(copyId != pixelsId);
        CHECK(AssetMeta::LoadFromFile(sources / "pixels_copy.png.meta")->id == *copyId);
    }

    SECTION("Changed settings are saved to the meta file") {
        database.GetMeta(*pixelsId)->texture.mipmaps = false;
        CHECK(database.SaveMeta(*pixelsId));
        CHECK_FALSE(AssetMeta::LoadFromFile(sources / "pixels.png.meta")->texture.mipmaps);
    }
}
