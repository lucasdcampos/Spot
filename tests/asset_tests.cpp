#include "temp_directory.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
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

TEST_CASE("Applications import their sources at startup and expose their assets", "[assets]") {
    const TempDirectory project;
    const auto sources = project.GetPath() / "assets";
    CreateProject(sources);

    class AssetApplication final : public Spot::Application {
    public:
        using Application::Application;

        bool hadDatabase = false;
        Spot::AssetRef<Spot::Model> model;

    protected:
        void OnStart() override {
            hadDatabase = GetAssetDatabase() != nullptr;
            model = Spot::Assets::Load<Spot::Model>("models/cube.glb");
            Quit();
        }
    };

    AssetApplication application(
        {.window = {.title = "Asset Tests"},
         .graphics = {.enabled = false},
         .audio = {.enabled = false},
         .assets = {.importDirectory = project.GetPath() / "imported", .sourceDirectory = sources}});
    REQUIRE(application.Run() == EXIT_SUCCESS);
    CHECK(application.hadDatabase);
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

    Spot::MaterialSource source{
        .baseColor = Spot::Color::Red, .baseColorTexture = pixelsId, .doubleSided = true, .worldTextureSize = 2.0f};
    const auto reloaded = Spot::MaterialSource::LoadFromString(source.SaveToString());
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->baseColorTexture == pixelsId);
    CHECK(reloaded->doubleSided);
    CHECK(reloaded->worldTextureSize == 2.0f);

    REQUIRE(source.SaveToFile(sources / "red.material"));
    CHECK(database.Refresh() == 1);

    Spot::AssetManager assets(imported);
    const auto material = assets.Load<Spot::Material>("red.material");
    REQUIRE(material);
    CHECK(material->baseColor == Spot::Color::Red);
    CHECK(material->doubleSided);
    CHECK(material->worldTextureSize == 2.0f);
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
