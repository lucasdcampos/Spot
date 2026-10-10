#include "assets/asset_formats.h"
#include "log_capture.h"
#include "temp_directory.h"

#include <spot.h>
#include <spot_import.h>

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

    // The .glb becomes a model with its texture, two materials and two meshes as sub-assets.
    const auto cubeId = database.FindByPath("models/cube.glb");
    REQUIRE(cubeId.has_value());
    const auto cubeParts = GetSubAssets(database, *cubeId);
    REQUIRE(cubeParts.size() == 5);
    const auto countOf = [&](AssetType type) {
        return std::ranges::count_if(cubeParts, [&](const Spot::AssetInfo& asset) { return asset.type == type; });
    };
    CHECK(countOf(AssetType::Texture) == 1);
    CHECK(countOf(AssetType::Material) == 2);
    CHECK(countOf(AssetType::Mesh) == 2);
    CHECK(GetSubAssets(database, *database.FindByPath("models/textured_quad.gltf")).size() == 3);

    REQUIRE(database.GetAssets().size() == 11);
    for (const auto& asset : database.GetAssets()) {
        CHECK(std::filesystem::exists(imported / (asset.id.ToString() + ".asset")));
        CHECK(database.Find(asset.id) == &asset);
    }
    // The meta file keeps the sub-assets' ids.
    CHECK(AssetMeta::LoadFromFile(sources / "models" / "cube.glb.meta")->subAssets.size() == 5);

    SECTION("Unchanged sources are not imported again, and keep their ids") {
        AssetDatabase reopened(sources, imported);
        CHECK(reopened.Refresh() == 0);
        CHECK(reopened.GetAssets().size() == 11);
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
    Spot::AssetManager manager(project.GetPath() / "imported");
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
    Spot::AssetManager manager(project.GetPath() / "imported");

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
    CHECK(std::distance(std::filesystem::directory_iterator(imported), std::filesystem::directory_iterator()) == 12);
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

    Spot::AssetManager assets(imported);
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
    Spot::AssetManager assets(imported);
    CHECK_FALSE(assets.Load<Spot::Material>(plainId));

    Spot::AssetManager empty(project.GetPath() / "nowhere");
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

    const Spot::AssetManager withoutFolder(project.GetPath() / "nowhere");
    REQUIRE(sink->GetRecords().size() == 1);
    CHECK(sink->GetRecords()[0].level == Spot::LogLevel::Debug);

    std::filesystem::create_directories(project.GetPath() / "imported");
    const Spot::AssetManager withEmptyFolder(project.GetPath() / "imported");
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
                                .doubleSided = true,
                                .worldTextureSize = 2.0f,
                                .specular = 0.5f,
                                .shininess = 64.0f};
    const auto reloaded = Spot::MaterialSource::LoadFromString(source.SaveToString());
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->baseColorTexture == pixelsId);
    CHECK(reloaded->doubleSided);
    CHECK(reloaded->worldTextureSize == 2.0f);
    CHECK(reloaded->specular == 0.5f);
    CHECK(reloaded->shininess == 64.0f);

    REQUIRE(source.SaveToFile(sources / "red.material"));
    CHECK(database.Refresh() == 1);

    Spot::AssetManager assets(imported);
    const auto material = assets.Load<Spot::Material>("red.material");
    REQUIRE(material);
    CHECK(material->baseColor == Spot::Color::Red);
    CHECK(material->doubleSided);
    CHECK(material->worldTextureSize == 2.0f);
    CHECK(material->specular == 0.5f);
    CHECK(material->shininess == 64.0f);
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
        Spot::AssetManager assets(imported);
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
    Spot::AssetManager assets(imported);
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
    Spot::AssetManager assets(imported);

    // Without a GPU the meshes do not load, but their references keep their ids.
    const auto model = assets.Load<Spot::Model>("models/cube.glb");
    REQUIRE(model);
    const AssetId meshId = model->GetParts()[1].mesh.GetId();

    Spot::Scene scene;
    Spot::Entity& cube = Spot::InstantiateModel(scene, *model, "Cube");
    REQUIRE(cube.GetChildren().size() == 2);
    // A material made in code is saved as null.
    scene.CreateEntity("Custom").AddComponent<Spot::MeshRenderer>(Spot::AssetRef<Spot::Mesh>(),
                                                                  Spot::Material{.baseColor = Spot::Color::Red});
    const std::string text = scene.SaveToString();
    CHECK(text.find(plainId.ToString()) != std::string::npos);
    CHECK(text.find(meshId.ToString()) != std::string::npos);

    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(text, &assets));
    const Spot::Entity* loadedCube = loaded.FindEntity("Cube");
    REQUIRE(loadedCube);
    REQUIRE(loadedCube->GetChildren().size() == 2);
    const auto* renderer = loadedCube->GetChildren()[1]->GetComponent<Spot::MeshRenderer>();
    REQUIRE(renderer);
    CHECK(renderer->GetMesh().GetId() == meshId);
    CHECK(renderer->GetMaterial().GetId() == plainId);
    // Loaded through the same manager, so shared with the model's.
    CHECK(renderer->GetMaterial().Get() == model->GetParts()[1].material.Get());

    const auto* custom = loaded.FindEntity("Custom")->GetComponent<Spot::MeshRenderer>();
    REQUIRE(custom);
    CHECK_FALSE(custom->GetMaterial());

    // Without an asset manager, the ids are kept so saving again loses nothing.
    Spot::Scene offline;
    REQUIRE(offline.LoadFromString(text));
    const auto* offlineRenderer = offline.FindEntity("Cube")->GetChildren()[1]->GetComponent<Spot::MeshRenderer>();
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
        Spot::AssetManager assets(imported);
        Spot::Scene level;
        level.CreateEntity("Floor").AddComponent<Spot::MeshRenderer>(Spot::AssetRef<Spot::Mesh>(),
                                                                     assets.Load<Spot::Material>(plainId));
        std::filesystem::create_directories(sources / "levels");
        REQUIRE(level.SaveToFile(sources / "levels" / "first.scene"));
    }
    std::ofstream(sources / "levels" / "broken.scene", std::ios::binary) << "{ nope";
    AssetDatabase database(sources, imported);
    CHECK(database.Refresh() == 1);

    Spot::AssetManager assets(imported);
    const auto first = assets.InstantiateScene("levels/first.scene");
    const auto second = assets.InstantiateScene("levels/first.scene");
    REQUIRE((first && second));
    CHECK(first.get() != second.get());
    const auto* floor = first->FindEntity("Floor");
    REQUIRE(floor);
    CHECK(floor->GetComponent<Spot::MeshRenderer>()->GetMaterial()->baseColor == Spot::Color::Blue);

    CHECK_FALSE(assets.InstantiateScene("levels/broken.scene"));
    CHECK_FALSE(assets.InstantiateScene("pixels.png"));
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
    Spot::AssetManager assets(imported);
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

    Spot::AssetManager assets(imported);
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

    Spot::AssetManager assets(imported);
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

    Spot::AssetManager assets(imported);
    CHECK(assets.FindByPath("models/cube.glb#Plain") == plainId);
    CHECK(assets.FindByPath("builtin/cube") == Spot::BuiltInAssets::CubeMesh);
    CHECK(assets.GetType(Spot::BuiltInAssets::SphereMesh) == AssetType::Mesh);
    CHECK(assets.FindByPath("builtin/grid") == Spot::BuiltInAssets::GridTexture);
    CHECK(assets.GetType(Spot::BuiltInAssets::CheckerTexture) == AssetType::Texture);
    // Built-in meshes and textures need the GPU like any other.
    CHECK_FALSE(assets.Load<Spot::Mesh>(Spot::BuiltInAssets::PlaneMesh));
    CHECK_FALSE(assets.Load<Spot::Texture>(Spot::BuiltInAssets::GridTexture));
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
