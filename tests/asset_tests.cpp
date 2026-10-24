#include "log_capture.h"
#include "registered_assets.h"
#include "spot/scene/physics/physics_world.h"
#include "temp_directory.h"
#include <spot/assets/asset_formats.h>
#include <spot/assets/asset_pak.h>
#include <spot/graphics/texture_asset_format.h>
#include <spot/import/importers/texture_compression.h>

#include <spot.h>
#include <spot_import.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

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
    CHECK(Spot::GetSourceAssetType("props/Crate.PREFAB") == AssetType::Prefab);
    CHECK_FALSE(Spot::GetSourceAssetType("house.glb.meta").has_value());
    CHECK_FALSE(Spot::GetSourceAssetType("buffer.bin").has_value());

    CHECK(Spot::ParseAssetType(Spot::GetAssetTypeName(AssetType::Mesh)) == AssetType::Mesh);
    CHECK(Spot::ParseAssetType(Spot::GetAssetTypeName(AssetType::Prefab)) == AssetType::Prefab);
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

    // The model's parts come right after it; the files' own assets come in the order of their paths.
    std::vector<const Spot::AssetInfo*> files;
    for (const auto& asset : database.GetAssets()) {
        if (!asset.parent.IsValid()) {
            files.push_back(&asset);
        }
    }
    REQUIRE(files.size() == 2);
    CHECK(files[0]->path == "models/cube.glb");
    CHECK(files[0]->type == AssetType::Model);
    CHECK(files[1]->path == "pixels.png");
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

namespace {

// A project with a texture, a .glb and a .gltf whose buffer and image are separate files.
void CreateProject(const std::filesystem::path& sources) {
    CopyFile(TestAssets / "pixels.png", sources / "pixels.png");
    CopyFile(TestAssets / "models" / "cube.glb", sources / "models" / "cube.glb");
    CopyFile(TestAssets / "models" / "textured_quad.gltf", sources / "models" / "textured_quad.gltf");
    CopyFile(TestAssets / "models" / "textured quad.bin", sources / "models" / "textured quad.bin");
}

// Copies, since refreshing the database replaces its list.
std::vector<Spot::AssetInfo> GetSubAssets(const AssetDatabase& database, AssetId parent) {
    std::vector<Spot::AssetInfo> subAssets;
    for (const auto& asset : database.GetAssets()) {
        if (asset.parent == parent) {
            subAssets.push_back(asset);
        }
    }
    return subAssets;
}

} // namespace

TEST_CASE("The asset database imports sources into the import directory", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    CreateProject(sources);

    AssetDatabase database(sources, imported);
    CHECK(database.Refresh() == 3);
    CHECK(std::filesystem::exists(imported / "manifest.json"));

    // The .glb becomes a model with its texture, two materials and its mesh, of two submeshes, as sub-assets.
    const auto cubeId = database.FindByPath("models/cube.glb");
    REQUIRE(cubeId.has_value());
    const auto cubeParts = GetSubAssets(database, *cubeId);
    REQUIRE(cubeParts.size() == 4);
    const auto countOf = [&](AssetType type) {
        return std::ranges::count_if(cubeParts, [&](const Spot::AssetInfo& asset) { return asset.type == type; });
    };
    CHECK(countOf(AssetType::Texture) == 1);
    CHECK(countOf(AssetType::Material) == 2);
    CHECK(countOf(AssetType::Mesh) == 1);
    CHECK(GetSubAssets(database, *database.FindByPath("models/textured_quad.gltf")).size() == 3);

    REQUIRE(database.GetAssets().size() == 10);
    for (const auto& asset : database.GetAssets()) {
        CHECK(std::filesystem::exists(imported / (asset.id.ToString() + ".asset")));
        CHECK(database.Find(asset.id) == &asset);
    }
    // The meta file keeps the sub-assets' ids.
    CHECK(AssetMeta::LoadFromFile(sources / "models" / "cube.glb.meta")->subAssets.size() == 4);

    SECTION("Unchanged sources are not imported again, and keep their ids") {
        AssetDatabase reopened(sources, imported);
        CHECK(reopened.Refresh() == 0);
        CHECK(reopened.GetAssets().size() == 10);
        for (const auto& part : cubeParts) {
            CHECK(reopened.Find(part.id) != nullptr);
        }
    }

    SECTION("Changed settings and dependencies import again, keeping sub-asset ids") {
        database.GetMeta(*cubeId)->model.scale = 2.0f;
        database.SaveMeta(*cubeId);
        CHECK(database.Refresh() == 1);
        for (const auto& part : GetSubAssets(database, *cubeId)) {
            CHECK(std::ranges::find(cubeParts, part.id, &Spot::AssetInfo::id) != cubeParts.end());
        }

        Spot::Tests::RegisteredAssetManager assets(imported);
        const auto model = assets.Load<Spot::Model>(*cubeId);
        REQUIRE(model);
        REQUIRE(model->GetNodes().size() == 2);
        CHECK(model->GetNodes()[1].parent == 0);
        CHECK(Spot::Distance(Spot::Decompose(model->GetNodeTransform(1)).translation, {22.0f, 0.0f, 0.0f}) < 1e-4f);
        const auto mesh = assets.LoadMeshData(model->GetNodes()[1].mesh.GetId());
        REQUIRE(mesh);
        CHECK(mesh->GetSubmeshes() == std::vector<Spot::SubMesh>{{0, 18}, {18, 18}});
        CHECK(mesh->GetBoundingBox().GetSize() == Spot::Vector3{2.0f, 2.0f, 2.0f});

        // The buffer may hold more bytes than the model uses, so one more byte keeps it valid.
        {
            std::ofstream buffer(sources / "models" / "textured quad.bin", std::ios::binary | std::ios::app);
            buffer.put('\0');
        }
        CHECK(database.Refresh() == 1);
        CHECK(database.Refresh() == 0);
    }

    SECTION("Reimporting runs the import even without changes") {
        CHECK(database.Reimport(*database.FindByPath("pixels.png")));
        CHECK_FALSE(database.Reimport(cubeParts.front().id));
        CHECK(database.Refresh() == 0);
    }

    SECTION("The imported files of removed sources are deleted") {
        std::filesystem::remove(sources / "models" / "cube.glb");
        CHECK(database.Refresh() == 0);
        CHECK(database.GetAssets().size() == 5);
        for (const auto& part : cubeParts) {
            CHECK_FALSE(std::filesystem::exists(imported / (part.id.ToString() + ".asset")));
        }
    }

    SECTION("Sources that fail to import are still listed") {
        std::ofstream(sources / "broken.png", std::ios::binary) << "not an image";
        CHECK(database.Refresh() == 0);
        const auto brokenId = database.FindByPath("broken.png");
        REQUIRE(brokenId.has_value());
        CHECK(database.Find(*brokenId) != nullptr);
        CHECK_FALSE(std::filesystem::exists(imported / (brokenId->ToString() + ".asset")));
    }
}

TEST_CASE("Imported model hierarchies reject invalid parents and truncated data", "[assets][model]") {
    std::vector<Spot::Detail::ModelFileNode> nodes{
        {.name = "Root", .transform = Spot::Matrix4::Translation({1.0f, 2.0f, 3.0f})},
        {.name = "Child",
         .transform = {},
         .parent = 0,
         .mesh = AssetId::Generate(),
         .materials = {AssetId::Generate()}}};
    Spot::Detail::BinaryWriter writer;
    Spot::Detail::Encode(writer, nodes);
    const auto loaded = Spot::Detail::DecodeModel(writer.GetData());
    REQUIRE(loaded);
    REQUIRE(loaded->size() == 2);
    CHECK((*loaded)[0].name == "Root");
    CHECK((*loaded)[0].transform == nodes[0].transform);
    CHECK((*loaded)[1].mesh == nodes[1].mesh);
    CHECK((*loaded)[1].materials == nodes[1].materials);
    const auto data = writer.GetData();
    for (std::size_t size = 0; size < data.size(); ++size) {
        CHECK_FALSE(Spot::Detail::DecodeModel(data.first(size)));
    }
    for (const int parent : {-2, 1, 2}) {
        nodes[1].parent = parent;
        Spot::Detail::BinaryWriter invalid;
        Spot::Detail::Encode(invalid, nodes);
        CHECK_FALSE(Spot::Detail::DecodeModel(invalid.GetData()));
    }
}

TEST_CASE("Tools move, rename, duplicate and delete sources, keeping their ids", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    CreateProject(sources);
    AssetDatabase database(sources, project.GetPath() / "imported");
    database.Refresh();
    const AssetId pixels = *database.FindByPath("pixels.png");
    const AssetId cube = *database.FindByPath("models/cube.glb");
    const auto cubeParts = GetSubAssets(database, cube);

    REQUIRE(database.CreateFolder("textures/ui"));
    CHECK(std::filesystem::is_directory(sources / "textures" / "ui"));

    // Files move with their meta files, and folders with everything in them.
    REQUIRE(database.Move("pixels.png", "textures/crate.png"));
    REQUIRE(database.Move("models", "meshes"));
    database.Refresh();
    CHECK(database.FindByPath("textures/crate.png") == pixels);
    CHECK(database.FindByPath("meshes/cube.glb") == cube);
    CHECK(GetSubAssets(database, cube).size() == cubeParts.size());
    CHECK_FALSE(std::filesystem::exists(sources / "pixels.png.meta"));

    // Only the case of a name may change, even where names ignore case.
    REQUIRE(database.Move("textures/crate.png", "textures/Crate.png"));
    database.Refresh();
    CHECK(database.FindByPath("textures/Crate.png") == pixels);

    // Nothing is moved over another file, into itself or out of the sources.
    CHECK_FALSE(database.Move("textures/Crate.png", "meshes/cube.glb"));
    CHECK_FALSE(database.Move("meshes", "meshes/inner"));
    CHECK_FALSE(database.Move("textures/Crate.png", "../outside.png"));
    CHECK_FALSE(database.Move("missing.png", "found.png"));

    // Duplicates are new assets imported the same way.
    database.GetMeta(pixels)->texture.mipmaps = false;
    database.SaveMeta(pixels);
    CHECK(database.GetFreePath("textures/Crate.png") == "textures/Crate 2.png");
    REQUIRE(database.Duplicate("textures/Crate.png", database.GetFreePath("textures/Crate.png")));
    database.Refresh();
    const auto copy = database.FindByPath("textures/Crate 2.png");
    REQUIRE(copy.has_value());
    CHECK(*copy != pixels);
    CHECK_FALSE(database.GetMeta(*copy)->texture.mipmaps);
    CHECK_FALSE(database.Duplicate("textures/Crate.png", "textures/Crate 2.png"));

    // Files from elsewhere are copied in under free names.
    const std::array outside{TestAssets / "pixels.png", TestAssets / "fonts"};
    const std::vector<std::string> copied = database.CopyIn(outside, "textures");
    CHECK(copied == std::vector<std::string>{"textures/pixels.png", "textures/fonts"});
    CHECK(database.CopyIn(std::span(outside).first(1), "textures") ==
          std::vector<std::string>{"textures/pixels 2.png"});
    database.Refresh();
    CHECK(database.FindByPath("textures/pixels 2.png").has_value());

    REQUIRE(database.Delete("textures"));
    REQUIRE(database.Delete("meshes/cube.glb"));
    database.Refresh();
    CHECK(database.Find(pixels) == nullptr);
    CHECK(database.Find(cube) == nullptr);
    CHECK_FALSE(std::filesystem::exists(sources / "meshes" / "cube.glb.meta"));
    CHECK_FALSE(database.Delete("textures"));
}

TEST_CASE("Sources whose files were only touched are not imported again", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    CreateProject(sources);
    AssetDatabase database(sources, project.GetPath() / "imported");
    CHECK(database.Refresh() == 3);

    // A new time of last change makes the contents be compared, which are the same.
    const auto file = sources / "pixels.png";
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) - std::chrono::hours(1));
    CHECK(database.Refresh() == 0);
    CHECK(database.Refresh() == 0);
}

TEST_CASE("Sources changed again within a tick of the file system's clock are imported again", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto file = sources / "paint.material";
    std::filesystem::create_directories(sources);
    REQUIRE(Spot::MaterialSource{.baseColor = Spot::Color::Blue}.SaveToFile(file));
    const auto time = std::filesystem::last_write_time(file);
    AssetDatabase database(sources, project.GetPath() / "imported");
    CHECK(database.Refresh() == 1);

    // The same size, and the same time, as quick edits can have.
    REQUIRE(Spot::MaterialSource{.baseColor = Spot::Color::Green}.SaveToFile(file));
    std::filesystem::last_write_time(file, time);
    CHECK(database.Refresh() == 1);
}

TEST_CASE("Changed assets load anew, with the loaded assets that refer to them", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    CopyFile(TestAssets / "pixels.png", sources / "pixels.png");
    AssetDatabase database(sources, project.GetPath() / "imported");
    database.Refresh();
    const AssetId texture = *database.FindByPath("pixels.png");
    REQUIRE(Spot::MaterialSource{.baseColorTexture = texture}.SaveToFile(sources / "paint.material"));
    REQUIRE(Spot::MaterialSource{}.SaveToFile(sources / "plain.material"));
    database.Refresh();
    const AssetId paint = *database.FindByPath("paint.material");
    const AssetId plain = *database.FindByPath("plain.material");
    CHECK(database.GetChanged().size() == 2);

    // Without a device the texture does not load, but materials still refer to it by id.
    Spot::Tests::RegisteredAssetManager manager(project.GetPath() / "imported");
    const auto paintBefore = manager.Load<Spot::Material>(paint);
    const auto plainBefore = manager.Load<Spot::Material>(plain);
    REQUIRE((paintBefore && plainBefore));

    REQUIRE(database.Reimport(texture));
    CHECK(std::vector(database.GetChanged().begin(), database.GetChanged().end()) == std::vector{texture});
    manager.RefreshManifest();
    const std::vector<AssetId> reloaded = manager.Reload(database.GetChanged());
    CHECK(std::ranges::find(reloaded, texture) != reloaded.end());
    CHECK(std::ranges::find(reloaded, paint) != reloaded.end());
    CHECK(std::ranges::find(reloaded, plain) == reloaded.end());
    CHECK(manager.Load<Spot::Material>(paint).Get() != paintBefore.Get());
    CHECK(manager.Load<Spot::Material>(plain).Get() == plainBefore.Get());

    // Removed sources change too.
    std::filesystem::remove(sources / "plain.material");
    database.Refresh();
    CHECK(std::ranges::find(database.GetChanged(), plain) != database.GetChanged().end());
}

TEST_CASE("Components load their references to changed assets again", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto file = sources / "paint.material";
    std::filesystem::create_directories(sources);
    REQUIRE(Spot::MaterialSource{.baseColor = Spot::Color{1.0f, 0.0f, 0.0f, 1.0f}}.SaveToFile(file));
    AssetDatabase database(sources, project.GetPath() / "imported");
    database.Refresh();
    const AssetId paint = *database.FindByPath("paint.material");
    Spot::Tests::RegisteredAssetManager manager(project.GetPath() / "imported");

    Spot::Scene scene;
    auto& renderer = scene.CreateEntity().AddComponent<Spot::MeshRenderer>(Spot::AssetRef<Spot::Mesh>(),
                                                                           manager.Load<Spot::Material>(paint));
    REQUIRE(renderer.GetMaterial());

    REQUIRE(Spot::MaterialSource{.baseColor = Spot::Color{0.0f, 0.0f, 1.0f, 1.0f}}.SaveToFile(file));
    // A time of change of its own, as a later edit would have.
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::hours(1));
    database.Refresh();
    manager.RefreshManifest();
    Spot::ReloadAssetReferences(renderer, manager.Reload(database.GetChanged()), &manager);
    REQUIRE(renderer.GetMaterial());
    CHECK(renderer.GetMaterial().GetId() == paint);
    CHECK(renderer.GetMaterial()->baseColor.b == 1.0f);

    // References to other assets are left as they are.
    const auto* material = renderer.GetMaterial().Get();
    Spot::ReloadAssetReferences(renderer, std::vector{AssetId::Generate()}, &manager);
    CHECK(renderer.GetMaterial().Get() == material);
}

TEST_CASE("The asset database keeps imported assets when its source directory is missing", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    CreateProject(sources);
    AssetDatabase(sources, imported).Refresh();

    AssetDatabase missing(project.GetPath() / "nowhere", imported);
    CHECK(missing.Refresh() == 0);
    CHECK(missing.GetAssets().empty());
    CHECK(std::filesystem::exists(imported / "manifest.json"));
    CHECK(std::distance(std::filesystem::directory_iterator(imported), std::filesystem::directory_iterator()) == 11);
}

namespace {

// Imports the test project and returns the id of the cube's untextured material.
AssetId ImportProject(const std::filesystem::path& sources, const std::filesystem::path& imported) {
    CreateProject(sources);
    AssetDatabase database(sources, imported);
    database.Refresh();
    for (const auto& asset : GetSubAssets(database, *database.FindByPath("models/cube.glb"))) {
        if (asset.name == "Plain") {
            return asset.id;
        }
    }
    return {};
}

} // namespace

TEST_CASE("The asset manager loads imported assets once and shares them", "[assets]") {
    const TempDirectory project;
    const auto imported = project.GetPath() / "imported";
    const AssetId plainId = ImportProject(project.GetPath() / "assets", imported);
    REQUIRE(plainId.IsValid());

    Spot::Tests::RegisteredAssetManager assets(imported);
    CHECK(assets.Contains(plainId));
    CHECK(assets.GetType(plainId) == AssetType::Material);
    CHECK(assets.FindByPath("models/cube.glb").has_value());
    CHECK_FALSE(assets.FindByPath("models/missing.glb").has_value());

    // Materials need no GPU.
    auto plain = assets.Load<Spot::Material>(plainId);
    REQUIRE(plain);
    CHECK(plain.GetId() == plainId);
    CHECK(plain->baseColor == Spot::Color::Blue);
    CHECK_FALSE(plain->baseColorTexture);
    CHECK(assets.Load<Spot::Material>(plainId).Get() == plain.Get());

    // Released assets are freed and loaded again when asked for.
    std::weak_ptr<const Spot::Material> released = plain.GetShared();
    plain = {};
    CHECK(released.expired());
    CHECK(assets.Load<Spot::Material>(plainId));

    // Without a graphics device, textures fail, but references keep their id.
    const auto pixelsId = *assets.FindByPath("pixels.png");
    const auto texture = assets.Load<Spot::Texture>(pixelsId);
    CHECK_FALSE(texture);
    CHECK(texture.GetId() == pixelsId);

    // So do models' meshes, while their materials load.
    const auto model = assets.Load<Spot::Model>("models/cube.glb");
    REQUIRE(model);
    REQUIRE(model->GetParts().size() == 2);
    CHECK_FALSE(model->GetParts()[0].mesh);
    CHECK(model->GetParts()[1].material.GetId() == plainId);

    CHECK_FALSE(assets.Load<Spot::Model>(plainId));
    CHECK_FALSE(assets.Load<Spot::Model>("models/missing.glb"));
    CHECK_FALSE(assets.Load<Spot::Material>(AssetId::Generate()));
}

TEST_CASE("The asset manager rejects corrupt imported files", "[assets]") {
    const TempDirectory project;
    const auto imported = project.GetPath() / "imported";
    const AssetId plainId = ImportProject(project.GetPath() / "assets", imported);

    std::ofstream(imported / (plainId.ToString() + ".asset"), std::ios::binary) << "SPOT garbage";
    Spot::Tests::RegisteredAssetManager assets(imported);
    CHECK_FALSE(assets.Load<Spot::Material>(plainId));

    Spot::Tests::RegisteredAssetManager empty(project.GetPath() / "nowhere");
    CHECK_FALSE(empty.Contains(plainId));
}

TEST_CASE("Asset managers only warn about a missing manifest when the import folder exists", "[assets]") {
    const TempDirectory project;
    const auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({sink});
    // Release builds hide debug messages by default.
    Spot::Logger& logger = Spot::Log::GetEngineLogger();
    const Spot::LogLevel level = logger.GetLevel();
    logger.SetLevel(Spot::LogLevel::Debug);

    const Spot::Tests::RegisteredAssetManager withoutFolder(project.GetPath() / "nowhere");
    REQUIRE(sink->GetRecords().size() == 1);
    CHECK(sink->GetRecords()[0].level == Spot::LogLevel::Debug);

    std::filesystem::create_directories(project.GetPath() / "imported");
    const Spot::Tests::RegisteredAssetManager withEmptyFolder(project.GetPath() / "imported");
    REQUIRE(sink->GetRecords().size() == 2);
    CHECK(sink->GetRecords()[1].level == Spot::LogLevel::Warning);
    logger.SetLevel(level);
}

TEST_CASE("Applications load the assets imported into their import directory", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    CreateProject(sources);
    AssetDatabase(sources, project.GetPath() / "imported").Refresh();

    class AssetApplication final : public Spot::Application {
    public:
        using Application::Application;

        Spot::AssetRef<Spot::Model> model;

    protected:
        void OnStart() override {
            model = Spot::Assets::Load<Spot::Model>("models/cube.glb");
            Quit();
        }
    };

    AssetApplication application({.window = {.title = "Asset Tests"},
                                  .graphics = {.enabled = false},
                                  .audio = {.enabled = false},
                                  .assets = {.importDirectory = project.GetPath() / "imported"}});
    REQUIRE(application.Run() == EXIT_SUCCESS);
    REQUIRE(application.model);
    CHECK(application.model->GetParts().size() == 2);
    CHECK(Spot::Assets::GetManager() == nullptr);
    CHECK_FALSE(Spot::Assets::Load<Spot::Material>("pixels.png"));
}

TEST_CASE("Material files are assets that refer to textures by id", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    CreateProject(sources);
    AssetDatabase database(sources, imported);
    database.Refresh();
    const AssetId pixelsId = *database.FindByPath("pixels.png");

    Spot::MaterialSource source{.baseColor = Spot::Color::Red,
                                .baseColorTexture = pixelsId,
                                .metallic = 1.0f,
                                .roughness = 0.25f,
                                .metallicRoughnessTexture = pixelsId,
                                .normalTexture = pixelsId,
                                .normalScale = 0.5f,
                                .occlusionTexture = pixelsId,
                                .occlusionStrength = 0.75f,
                                .emissive = Spot::Color{2.0f, 1.0f, 0.0f, 1.0f},
                                .emissiveTexture = pixelsId,
                                .alphaMode = Spot::AlphaMode::Cutout,
                                .alphaCutoff = 0.25f,
                                .doubleSided = true,
                                .worldTextureSize = 2.0f,
                                .nearestFiltering = true};
    const auto reloaded = Spot::MaterialSource::LoadFromString(source.SaveToString());
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->baseColorTexture == pixelsId);
    CHECK(reloaded->metallic == 1.0f);
    CHECK(reloaded->roughness == 0.25f);
    CHECK(reloaded->metallicRoughnessTexture == pixelsId);
    CHECK(reloaded->normalTexture == pixelsId);
    CHECK(reloaded->normalScale == 0.5f);
    CHECK(reloaded->occlusionTexture == pixelsId);
    CHECK(reloaded->occlusionStrength == 0.75f);
    CHECK(reloaded->emissive == Spot::Color{2.0f, 1.0f, 0.0f, 1.0f});
    CHECK(reloaded->emissiveTexture == pixelsId);
    CHECK(reloaded->alphaMode == Spot::AlphaMode::Cutout);
    CHECK(reloaded->alphaCutoff == 0.25f);
    CHECK(reloaded->doubleSided);
    CHECK(reloaded->worldTextureSize == 2.0f);
    CHECK(reloaded->nearestFiltering);
    CHECK_FALSE(Spot::MaterialSource::LoadFromString("{}")->nearestFiltering);

    REQUIRE(source.SaveToFile(sources / "red.material"));
    CHECK(database.Refresh() == 1);

    Spot::Tests::RegisteredAssetManager assets(imported);
    const auto material = assets.Load<Spot::Material>("red.material");
    REQUIRE(material);
    CHECK(material->baseColor == Spot::Color::Red);
    CHECK(material->doubleSided);
    CHECK(material->worldTextureSize == 2.0f);
    CHECK(material->nearestFiltering);
    CHECK(material->metallic == 1.0f);
    CHECK(material->roughness == 0.25f);
    CHECK(material->normalScale == 0.5f);
    CHECK(material->occlusionStrength == 0.75f);
    CHECK(material->emissive == Spot::Color{2.0f, 1.0f, 0.0f, 1.0f});
    CHECK(material->normalTexture.GetId() == pixelsId);
    CHECK(material->alphaMode == Spot::AlphaMode::Cutout);
    CHECK(material->alphaCutoff == 0.25f);
    // There is no GPU here to load the texture, but the reference knows which one it is.
    CHECK(material->baseColorTexture.GetId() == pixelsId);

    std::ofstream(sources / "broken.material", std::ios::binary) << "[1, 2";
    CHECK(database.Refresh() == 0);
}

TEST_CASE("Materials extracted from a model replace the model's own", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    const AssetId plainId = ImportProject(sources, imported);
    AssetDatabase database(sources, imported);
    database.Refresh();
    const AssetId cubeId = *database.FindByPath("models/cube.glb");

    const auto extracted = database.ExtractMaterial(plainId, "materials/plain.material");
    REQUIRE(extracted.has_value());
    CHECK(database.FindByPath("materials/plain.material") == extracted);
    REQUIRE(database.GetMeta(cubeId)->model.materials.size() == 1);
    CHECK(database.GetMeta(cubeId)->model.materials[0].material == *extracted);
    CHECK(Spot::MaterialSource::LoadFromFile(sources / "materials" / "plain.material")->baseColor == Spot::Color::Blue);

    // The model's own material is still imported, so it can be extracted again, but the model uses the file.
    {
        Spot::Tests::RegisteredAssetManager assets(imported);
        CHECK(assets.Contains(plainId));
        const auto model = assets.Load<Spot::Model>(cubeId);
        REQUIRE(model);
        CHECK(model->GetParts()[1].material.GetId() == *extracted);
    }

    // Editing the file changes the model's look.
    auto edited = *Spot::MaterialSource::LoadFromFile(sources / "materials" / "plain.material");
    edited.baseColor = Spot::Color::Green;
    edited.SaveToFile(sources / "materials" / "plain.material");
    CHECK(database.Refresh() == 1);
    Spot::Tests::RegisteredAssetManager assets(imported);
    CHECK(assets.Load<Spot::Model>(cubeId)->GetParts()[1].material->baseColor == Spot::Color::Green);

    CHECK_FALSE(database.ExtractMaterial(plainId, "materials/plain.material").has_value());
    CHECK_FALSE(database.ExtractMaterial(cubeId, "materials/cube.material").has_value());
    CHECK_FALSE(database.ExtractMaterial(plainId, "materials/plain.png").has_value());
}

TEST_CASE("Scenes save mesh renderers by asset id and load them back through the asset manager", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    const AssetId plainId = ImportProject(sources, imported);
    Spot::Tests::RegisteredAssetManager assets(imported);

    // Without a GPU the meshes do not load, but their references keep their ids.
    const auto model = assets.Load<Spot::Model>("models/cube.glb");
    REQUIRE(model);
    const AssetId meshId = model->GetParts()[1].mesh.GetId();

    Spot::Scene scene;
    Spot::Entity& cube = scene.Instantiate(*model, "Cube");
    REQUIRE(cube.GetChildren().size() == 1);
    REQUIRE(cube.FindChild("Pivot"));
    REQUIRE(cube.FindChild("Pivot")->FindChild("Cube"));
    REQUIRE(cube.FindChild("Pivot")->FindChild("Cube")->GetComponents<Spot::MeshRenderer>().size() == 2);
    // A material made in code is saved inline, without a material asset id.
    scene.CreateEntity("Custom").AddComponent<Spot::MeshRenderer>(
        Spot::AssetRef<Spot::Mesh>(), Spot::Material{.baseColor = Spot::Color::Red, .nearestFiltering = true});
    const std::string text = scene.SaveToString();
    CHECK(text.find(plainId.ToString()) != std::string::npos);
    CHECK(text.find(meshId.ToString()) != std::string::npos);

    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(text, &assets));
    const Spot::Entity* loadedCube = loaded.FindEntity("Cube");
    REQUIRE(loadedCube);
    REQUIRE(loadedCube->GetChildren().size() == 1);
    const auto renderers = loadedCube->FindChild("Pivot")->FindChild("Cube")->GetComponents<Spot::MeshRenderer>();
    REQUIRE(renderers.size() == 2);
    const auto* renderer = renderers[1];
    REQUIRE(renderer);
    CHECK(renderer->GetMesh().GetId() == meshId);
    CHECK(renderer->GetMaterial().GetId() == plainId);
    CHECK(renderer->GetSubmesh() == 1);
    // Loaded through the same manager, so shared with the model's.
    CHECK(renderer->GetMaterial().Get() == model->GetParts()[1].material.Get());

    const auto* custom = loaded.FindEntity("Custom")->GetComponent<Spot::MeshRenderer>();
    REQUIRE(custom);
    REQUIRE(custom->GetMaterial());
    CHECK_FALSE(custom->GetMaterial().GetId().IsValid());
    CHECK(custom->GetMaterial()->baseColor == Spot::Color::Red);
    CHECK(custom->GetMaterial()->nearestFiltering);

    // Without an asset manager, the ids are kept so saving again loses nothing.
    Spot::Scene offline;
    REQUIRE(offline.LoadFromString(text));
    const auto* offlineRenderer =
        offline.FindEntity("Cube")->FindChild("Pivot")->FindChild("Cube")->GetComponents<Spot::MeshRenderer>()[1];
    CHECK_FALSE(offlineRenderer->GetMaterial());
    CHECK(offlineRenderer->GetMaterial().GetId() == plainId);
    CHECK(offline.SaveToString().find(plainId.ToString()) != std::string::npos);
}

TEST_CASE("Scene files are assets that instantiate into new scenes", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    const AssetId plainId = ImportProject(sources, imported);

    {
        Spot::Tests::RegisteredAssetManager assets(imported);
        Spot::Scene level;
        level.CreateEntity("Floor").AddComponent<Spot::MeshRenderer>(Spot::AssetRef<Spot::Mesh>(),
                                                                     assets.Load<Spot::Material>(plainId));
        std::filesystem::create_directories(sources / "levels");
        REQUIRE(level.SaveToFile(sources / "levels" / "first.scene"));
    }
    std::ofstream(sources / "levels" / "broken.scene", std::ios::binary) << "{ nope";
    AssetDatabase database(sources, imported);
    CHECK(database.Refresh() == 1);

    Spot::Tests::RegisteredAssetManager assets(imported);
    const auto first = Spot::InstantiateScene(assets, "levels/first.scene");
    const auto second = Spot::InstantiateScene(assets, "levels/first.scene");
    REQUIRE((first && second));
    CHECK(first.get() != second.get());
    const auto* floor = first->FindEntity("Floor");
    REQUIRE(floor);
    CHECK(floor->GetComponent<Spot::MeshRenderer>()->GetMaterial()->baseColor == Spot::Color::Blue);

    CHECK_FALSE(Spot::InstantiateScene(assets, "levels/broken.scene"));
    CHECK_FALSE(Spot::InstantiateScene(assets, "pixels.png"));
    CHECK_FALSE(Spot::Assets::InstantiateScene("levels/first.scene"));
}

TEST_CASE("Sounds are imported decoded and load without a decoder", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    CopyFile(TestAssets / "sine.ogg", sources / "sine.ogg");
    AssetDatabase database(sources, imported);
    CHECK(database.Refresh() == 1);

    const auto original = Spot::Sound::Load(TestAssets / "sine.ogg");
    REQUIRE(original);
    Spot::Tests::RegisteredAssetManager assets(imported);
    const auto sound = assets.Load<Spot::Sound>("sine.ogg");
    REQUIRE(sound);
    CHECK(sound->GetSampleRate() == original->GetSampleRate());
    CHECK(sound->GetChannelCount() == original->GetChannelCount());
    REQUIRE(sound->GetSamples().size() == original->GetSamples().size());
    for (std::size_t i = 0; i < original->GetSamples().size(); i += 97) {
        CHECK(std::abs(sound->GetSamples()[i] - original->GetSamples()[i]) < 1e-4f);
    }
}

TEST_CASE("Sounds set to stream are imported encoded and stream when they load", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    CopyFile(TestAssets / "sine.ogg", sources / "sine.ogg");
    AssetDatabase database(sources, imported);
    CHECK(database.Refresh() == 1);
    const auto id = database.FindByPath("sine.ogg");
    REQUIRE(id.has_value());
    database.GetMeta(*id)->sound.stream = true;
    REQUIRE(database.SaveMeta(*id));
    CHECK(database.Refresh() == 1);
    CHECK(AssetMeta::LoadFromFile(sources / "sine.ogg.meta")->sound.stream);

    Spot::Tests::RegisteredAssetManager assets(imported);
    const auto sound = assets.Load<Spot::Sound>("sine.ogg");
    REQUIRE(sound);
    CHECK(sound->IsStreamed());
    CHECK(sound->GetSampleRate() == 44100);
    CHECK(sound->GetFrameCount() == 11025);
    // The imported file holds the source's data as it was.
    CHECK(sound->GetEncodedData().size() == std::filesystem::file_size(sources / "sine.ogg"));
}

TEST_CASE("Fonts are imported with extra sizes as sub-assets", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    CopyFile(TestAssets / "fonts" / "FiraSans-Regular.ttf", sources / "fonts" / "FiraSans-Regular.ttf");
    CopyFile(TestAssets / "fonts" / "OFL.txt", sources / "fonts" / "NotAFont.ttf");
    AssetDatabase database(sources, imported);
    CHECK(database.Refresh() == 1);

    const AssetId fontId = *database.FindByPath("fonts/FiraSans-Regular.ttf");
    database.GetMeta(fontId)->font.pixelSize = 20.0f;
    database.GetMeta(fontId)->font.extraSizes = {48.0f};
    database.SaveMeta(fontId);
    CHECK(database.Refresh() == 1);

    const auto large = database.FindByPath("fonts/FiraSans-Regular.ttf#48px");
    REQUIRE(large.has_value());
    CHECK(database.Find(*large)->parent == fontId);
    CHECK_FALSE(database.FindByPath("fonts/FiraSans-Regular.ttf#12px").has_value());

    Spot::Tests::RegisteredAssetManager assets(imported);
    CHECK(assets.FindByPath("fonts/FiraSans-Regular.ttf#48px") == large);
    CHECK(assets.GetType(*large) == AssetType::Font);
}

TEST_CASE("Sub-assets and built-in assets are found by path", "[assets]") {
    const TempDirectory project;
    const auto imported = project.GetPath() / "imported";
    const AssetId plainId = ImportProject(project.GetPath() / "assets", imported);
    AssetDatabase database(project.GetPath() / "assets", imported);
    database.Refresh();
    CHECK(database.FindByPath("models/cube.glb#Plain") == plainId);
    CHECK_FALSE(database.FindByPath("models/cube.glb#").has_value());

    Spot::Tests::RegisteredAssetManager assets(imported);
    CHECK(assets.FindByPath("models/cube.glb#Plain") == plainId);
    CHECK(assets.FindByPath("builtin/cube") == Spot::BuiltInAssets::CubeMesh);
    CHECK(assets.GetType(Spot::BuiltInAssets::SphereMesh) == AssetType::Mesh);
    CHECK(assets.FindByPath("builtin/grid") == Spot::BuiltInAssets::GridTexture);
    CHECK(assets.GetType(Spot::BuiltInAssets::CheckerTexture) == AssetType::Texture);
    // Built-in meshes and textures need the GPU like any other.
    CHECK_FALSE(assets.Load<Spot::Mesh>(Spot::BuiltInAssets::PlaneMesh));
    CHECK_FALSE(assets.Load<Spot::Texture>(Spot::BuiltInAssets::GridTexture));
}

TEST_CASE("Runtime asset catalogs query the manifest by type and directory", "[assets][prefab]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    Spot::Scene source;
    source.CreateEntity("Template");
    const auto write = [&](const std::filesystem::path& path) {
        std::filesystem::create_directories((sources / path).parent_path());
        REQUIRE(source.SaveToFile(sources / path));
    };
    write("prefabs/z.prefab");
    write("prefabs/nested/a.prefab");
    write("prefabs_extra/outside.prefab");
    write("prefabs/level.scene");
    AssetDatabase database(sources, imported);
    database.Refresh();
    Spot::Tests::RegisteredAssetManager assets(imported);
    const AssetId nested = *database.FindByPath("prefabs/nested/a.prefab");
    const AssetId last = *database.FindByPath("prefabs/z.prefab");
    const AssetId outside = *database.FindByPath("prefabs_extra/outside.prefab");
    const std::vector<AssetId> expected{nested, last};
    CHECK(assets.GetAssetIds(AssetType::Prefab, "prefabs") == expected);
    CHECK(assets.GetAssetIds(AssetType::Prefab, "prefabs///") == expected);
    CHECK(assets.GetAssetIds(AssetType::Prefab, "prefabs\\nested\\") == std::vector<AssetId>{nested});
    CHECK(assets.GetAssetIds(AssetType::Prefab) == std::vector<AssetId>{nested, last, outside});
    CHECK(assets.GetAssetIds(AssetType::Prefab, "missing").empty());
    CHECK(assets.GetAssetIds(AssetType::Mesh).empty());
    CHECK(assets.Contains(Spot::BuiltInAssets::CubeMesh));
    CHECK(assets.GetAssetIds(AssetType::Scene, "prefabs").size() == 1);

    write("prefabs/b.prefab");
    database.Refresh();
    CHECK(assets.GetAssetIds(AssetType::Prefab, "prefabs") == expected);
    assets.RefreshManifest();
    const AssetId added = *database.FindByPath("prefabs/b.prefab");
    const std::vector<AssetId> refreshed{added, nested, last};
    CHECK(assets.GetAssetIds(AssetType::Prefab, "prefabs") == refreshed);

    // A shipped game has only imported files; catalog queries must not inspect source files.
    std::filesystem::remove_all(sources);
    Spot::Tests::RegisteredAssetManager shipped(imported);
    CHECK(shipped.GetAssetIds(AssetType::Prefab, "prefabs") == refreshed);
    CHECK(shipped.Load<Spot::Prefab>(added));
}

TEST_CASE("Applications run a game from its startup scene", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    {
        Spot::Scene level;
        level.CreateEntity("Hero");
        std::filesystem::create_directories(sources);
        REQUIRE(level.SaveToFile(sources / "first.scene"));
    }
    AssetDatabase database(sources, imported);
    database.Refresh();
    const auto startup = database.FindByPath("first.scene");
    REQUIRE(startup.has_value());

    class GameApplication final : public Spot::Application {
    public:
        using Application::Application;

        std::string title;
        bool loadedHero = false;

    protected:
        void OnStart() override { title = std::string(GetWindow().GetTitle()); }

        void OnUpdate(float) override {
            if (const Spot::Scene* scene = GetScene()) {
                loadedHero = scene->FindEntity("Hero") != nullptr;
                Quit();
            } else if (++m_frames > 10) {
                Quit();
            }
        }

    private:
        int m_frames = 0;
    };

    const Spot::ApplicationConfig config{
        .graphics = {.enabled = false}, .audio = {.enabled = false}, .assets = {.importDirectory = imported}};
    // The game says where its imported assets are, when the application does not.
    Spot::ApplicationConfig game = config;
    game.assets.importDirectory.clear();
    game.game =
        Spot::GameSettings{.name = "Heroic", .startupScene = *startup, .importDirectory = Spot::PathToUtf8(imported)};
    GameApplication application(game);
    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(application.loadedHero);
    CHECK(application.title == "Heroic");

    Spot::ApplicationConfig missing = config;
    missing.game = Spot::GameSettings{.name = "Lost", .startupScene = Spot::AssetId::Generate()};
    GameApplication lost(missing);
    CHECK(lost.Run() == EXIT_FAILURE);
    CHECK_FALSE(lost.loadedHero);
}

TEST_CASE("Textures set to hold data import to be read as they are", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    CopyFile(TestAssets / "pixels.png", sources / "lut.png");
    AssetDatabase database(sources, imported);
    database.Refresh();
    const auto id = database.FindByPath("lut.png");
    REQUIRE(id.has_value());
    const auto read = [&] {
        const auto data =
            Spot::Detail::ReadAssetFile(Spot::Detail::GetImportedAssetPath(imported, *id), AssetType::Texture);
        REQUIRE(data.has_value());
        const auto texture = Spot::Detail::DecodeTexture(*data);
        REQUIRE(texture.has_value());
        return texture->srgb;
    };
    CHECK(read());

    database.GetMeta(*id)->texture.srgb = false;
    REQUIRE(database.SaveMeta(*id));
    CHECK(database.Refresh() == 1);
    CHECK_FALSE(read());
}

TEST_CASE("Materials saved before PBR get a roughness from their specular and shininess", "[assets]") {
    const auto glossy = Spot::MaterialSource::LoadFromString(R"({"specular": 0.5, "shininess": 254.0})");
    REQUIRE(glossy.has_value());
    CHECK(glossy->roughness == Catch::Approx(std::pow(2.0f / 256.0f, 0.25f)));
    CHECK(glossy->metallic == 0.0f);

    const auto rough = Spot::MaterialSource::LoadFromString(R"({"specular": 0.15, "shininess": 16.0})");
    REQUIRE(rough.has_value());
    CHECK(rough->roughness > glossy->roughness);

    // Without highlights, they were matte.
    const auto matte = Spot::MaterialSource::LoadFromString(R"({"specular": 0.0, "shininess": 32.0})");
    REQUIRE(matte.has_value());
    CHECK(matte->roughness == 1.0f);

    // A roughness, once saved, is kept.
    const auto saved = Spot::MaterialSource::LoadFromString(R"({"roughness": 0.3, "specular": 0.0})");
    REQUIRE(saved.has_value());
    CHECK(saved->roughness == Catch::Approx(0.3f));
}

TEST_CASE("Textures with mipmaps import block-compressed, with every level, and decode back close", "[assets]") {
    std::vector<std::uint8_t> pixels(16 * 8 * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = 200;
        pixels[i + 1] = 60;
        pixels[i + 2] = 30;
        pixels[i + 3] = 255;
    }
    const Spot::Image opaque(16, 8, pixels);
    CHECK(Spot::Detail::CanCompressTexture(opaque, true));
    CHECK_FALSE(Spot::Detail::CanCompressTexture(opaque, false));
    CHECK_FALSE(Spot::Detail::CanCompressTexture(Spot::Image(6, 8, std::vector<std::uint8_t>(6 * 8 * 4)), true));

    const auto file = Spot::Detail::CompressTexture(opaque, true);
    REQUIRE(file.compressedFormat == Spot::TextureFormat::Bc1Srgb);
    // 16x8, 8x4, 4x2, 2x1 and 1x1.
    REQUIRE(file.levels.size() == 5);
    CHECK(file.levels[0].size() == 4 * 2 * 8);

    Spot::Detail::BinaryWriter writer;
    Spot::Detail::Encode(writer, file);
    const auto decoded = Spot::Detail::DecodeTexture(writer.GetData());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->levels == file.levels);
    const auto back =
        Spot::Detail::DecompressBlocks(*decoded->compressedFormat, decoded->width, decoded->height, decoded->levels[0]);
    REQUIRE(back.size() == pixels.size());
    for (std::size_t i = 0; i < back.size(); ++i) {
        CHECK(std::abs(static_cast<int>(back[i]) - pixels[i]) <= 8);
    }

    pixels[3] = 100;
    const auto translucent = Spot::Detail::CompressTexture(Spot::Image(16, 8, pixels), false);
    CHECK(translucent.compressedFormat == Spot::TextureFormat::Bc3);
    const auto alpha = Spot::Detail::DecompressBlocks(Spot::TextureFormat::Bc3, 16, 8, translucent.levels[0]);
    CHECK(std::abs(static_cast<int>(alpha[3]) - 100) <= 8);
    CHECK(alpha[7] == 255);
}

TEST_CASE("Compressed cutout textures can keep their alpha coverage in smaller mipmaps", "[assets]") {
    // Scattered opaque pixels, about three in ten, as leaves with gaps between them: averaged, most of the smaller
    // levels' pixels fall below half alpha unless their coverage is kept.
    constexpr std::uint32_t Size = 64;
    std::vector<std::uint8_t> pixels(Size * Size * 4, 0);
    std::uint32_t state = 12345;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        state = state * 1664525u + 1013904223u;
        pixels[i + 1] = 160;
        pixels[i + 3] = (state >> 24) < 77 ? 255 : 0;
    }
    const auto coverage = [](const Spot::Detail::TextureFile& file, std::size_t level) {
        const std::uint32_t size = Size >> level;
        const auto decoded = Spot::Detail::DecompressBlocks(Spot::TextureFormat::Bc3, size, size, file.levels[level]);
        std::size_t covered = 0;
        for (std::size_t i = 3; i < decoded.size(); i += 4) {
            covered += decoded[i] >= 128;
        }
        return static_cast<float>(covered) / static_cast<float>(size * size);
    };
    const Spot::Image image(Size, Size, pixels);
    const auto plain = Spot::Detail::CompressTexture(image, true);
    const auto kept = Spot::Detail::CompressTexture(image, true, true);
    const float full = coverage(kept, 0);
    INFO("Full size " << full << "; at 16x16, " << coverage(plain, 2) << " plain and " << coverage(kept, 2) << " kept");
    CHECK(coverage(kept, 0) == coverage(plain, 0));
    CHECK(coverage(plain, 2) < full * 0.5f);
    CHECK(coverage(kept, 2) > full * 0.75f);
    CHECK(coverage(kept, 2) < full * 1.5f);
}

TEST_CASE("Paks hold the imported assets in one file, which the asset manager loads them from", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    CopyFile(TestAssets / "pixels.png", sources / "pixels.png");
    CopyFile(TestAssets / "sine.ogg", sources / "sine.ogg");
    AssetDatabase database(sources, imported);
    database.Refresh();
    const auto texture = database.FindByPath("pixels.png");
    const auto sound = database.FindByPath("sine.ogg");
    REQUIRE((texture && sound));
    const auto loose = Spot::Detail::ReadFileBytes(Spot::Detail::GetImportedAssetPath(imported, *texture));
    REQUIRE(loose.has_value());

    REQUIRE(Spot::Detail::WriteAssetPak(imported / Spot::Detail::AssetPakFileName, imported));
    const auto pak = Spot::Detail::AssetPak::Open(imported / Spot::Detail::AssetPakFileName);
    REQUIRE(pak);
    CHECK(pak->GetAssetCount() == 2);
    CHECK(pak->Read(*texture) == *loose);
    CHECK_FALSE(pak->Read(Spot::AssetId::Generate()).has_value());

    // Without their own files, assets still load, from the pak.
    std::filesystem::remove(Spot::Detail::GetImportedAssetPath(imported, *texture));
    std::filesystem::remove(Spot::Detail::GetImportedAssetPath(imported, *sound));
    Spot::Tests::RegisteredAssetManager assets(imported);
    CHECK(assets.ReadImportedData(*texture, AssetType::Texture).has_value());
    CHECK(assets.Load<Spot::Sound>(*sound));
}

TEST_CASE("Assets load in the background, and preload together with what they refer to", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    CopyFile(TestAssets / "sine.ogg", sources / "sine.ogg");
    CopyFile(TestAssets / "sine.ogg", sources / "other.ogg");
    AssetDatabase database(sources, imported);
    database.Refresh();
    const auto sine = database.FindByPath("sine.ogg");
    const auto other = database.FindByPath("other.ogg");
    REQUIRE((sine && other));
    Spot::Tests::RegisteredAssetManager assets(imported);

    auto request = assets.LoadAsync<Spot::Sound>(*sine);
    REQUIRE(request.IsValid());
    CHECK_FALSE(assets.IsLoaded(*sine));
    // Made by Update(), once read on another thread.
    for (int i = 0; i < 1000 && !request.IsDone(); ++i) {
        assets.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(request.IsDone());
    const auto sound = request.Get();
    CHECK(sound);
    CHECK(assets.IsLoaded(*sine));
    // Loaded already, it is done at once, and the same asset.
    const auto again = assets.LoadAsync<Spot::Sound>(*sine);
    CHECK(again.IsDone());
    CHECK(again.Get().Get() == sound.Get());

    // Text naming assets by id preloads them, while what it returns is kept.
    const auto preloaded = assets.PreloadReferencedBy("{\"sound\": \"" + other->ToString() + "\"}");
    CHECK(preloaded.size() == 1);
    CHECK(assets.IsLoaded(*other));
    CHECK(assets.Load<Spot::Sound>(*other).Get() == preloaded.front().get());
}

TEST_CASE("Scenes load chunks in the background into their own hierarchy", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    std::filesystem::create_directories(sources);
    {
        Spot::Scene start;
        start.CreateEntity("Player").AddComponent<Spot::BoxCollider>();
        REQUIRE(start.SaveToFile(sources / "start.scene"));
        Spot::Scene district;
        auto& market = district.CreateEntity("Market");
        market.GetTransform().SetPosition({3, 0, 0});
        market.AddComponent<Spot::BoxCollider>();
        REQUIRE(district.SaveToFile(sources / "district.scene"));
    }
    AssetDatabase database(sources, imported);
    database.Refresh();
    const auto start = database.FindByPath("start.scene");
    const auto district = database.FindByPath("district.scene");
    REQUIRE((start && district));

    class StreamingApplication final : public Spot::Application {
    public:
        StreamingApplication(const Spot::ApplicationConfig& config, Spot::AssetId district)
            : Application(config), m_district(district) {}

        bool streamed = false;
        bool playerKept = false;
        int framesWaited = 0;

    protected:
        void OnUpdate(float) override {
            if (!m_load.IsValid()) {
                m_load = GetScene()->LoadChunkAsync(GetAssets(), m_district);
                SetScenesPaused(true);
                return;
            }
            if (!m_load.IsDone()) {
                if (++framesWaited > 1000) {
                    Quit();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return;
            }
            // Loaded alongside the scene already there, which kept running.
            auto* root = m_load.GetRoot();
            streamed = root && root->FindChild("Market") && &root->GetScene() == GetScene();
            if (root) {
                const auto hit = GetScene()->GetOrAddSystem<Spot::PhysicsWorld>().Raycast({3, 5, 0}, {0, -1, 0}, 10);
                REQUIRE(hit);
                CHECK(hit->entity == root->FindChild("Market")->GetId());
                CHECK(GetScene()->GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount() == 2);
                root->Destroy();
                CHECK(m_load.GetRoot() == nullptr);
                CHECK(GetScene()->FindEntity("Market") == nullptr);
                CHECK_FALSE(GetScene()->GetOrAddSystem<Spot::PhysicsWorld>().Raycast({3, 5, 0}, {0, -1, 0}, 10));
            }
            playerKept = GetScene() && GetScene()->FindEntity("Player");
            Quit();
        }

    private:
        Spot::AssetId m_district;
        Spot::ChunkLoad m_load;
    };

    Spot::ApplicationConfig config{
        .graphics = {.enabled = false}, .audio = {.enabled = false}, .assets = {.importDirectory = imported}};
    config.game = Spot::GameSettings{.name = "Streaming", .startupScene = *start};
    StreamingApplication application(config, *district);
    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(application.streamed);
    CHECK(application.playerKept);
}

TEST_CASE("Chunk handles survive failure, cancellation and scene destruction", "[assets]") {
    const TempDirectory project;
    Spot::AssetManager assets(project.GetPath());
    Spot::ChunkLoad cancelled;
    {
        Spot::Scene scene;
        cancelled = scene.LoadChunkAsync(assets, Spot::AssetId::Generate());
        REQUIRE(cancelled.IsValid());
        scene.Clear();
        CHECK(cancelled.IsDone());
        CHECK(cancelled.GetRoot() == nullptr);
        scene.Update(0.0f);
        CHECK(scene.GetEntityCount() == 0);
    }
    CHECK(cancelled.GetRoot() == nullptr);
    Spot::ChunkLoad failed;
    {
        Spot::Scene scene;
        failed = scene.LoadChunkAsync(assets, Spot::AssetId::Generate());
        for (int frame = 0; frame < 1000 && !failed.IsDone(); ++frame) {
            scene.Update(0.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(failed.IsDone());
        CHECK(failed.GetRoot() == nullptr);
        CHECK(scene.GetEntityCount() == 0);
    }
    CHECK(failed.GetRoot() == nullptr);
}

TEST_CASE("Standalone scenes load empty chunks and invalidate completed handles on destruction", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    const auto imported = project.GetPath() / "imported";
    std::filesystem::create_directories(sources);
    REQUIRE(Spot::Scene().SaveToFile(sources / "empty.scene"));
    AssetDatabase database(sources, imported);
    database.Refresh();
    const auto id = database.FindByPath("empty.scene");
    REQUIRE(id);
    Spot::AssetManager assets(imported);
    Spot::ChunkLoad load;
    {
        Spot::Scene scene;
        load = scene.LoadChunkAsync(assets, *id);
        for (int frame = 0; frame < 1000 && !load.IsDone(); ++frame) {
            scene.Update(0.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(load.IsDone());
        REQUIRE(load.GetRoot());
        CHECK(load.GetRoot()->GetChildren().empty());
        CHECK(scene.GetEntityCount() == 1);
    }
    CHECK(load.IsDone());
    CHECK(load.GetRoot() == nullptr);
}
