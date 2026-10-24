#include "headless_imgui.h"
#include "script_asset_fixture.h"
#include "temp_directory.h"

#include "app/editor_context.h"
#include "assets/asset_browser_model.h"
#include "assets/plain_files.h"
#include "code/script_editor.h"
#include "imgui/asset_icons.h"
#include "panels/asset_browser_panel.h"
#include "panels/code_editor_panel.h"
#include "panels/hierarchy_panel.h"
#include "panels/inspector_panel.h"
#include "project/editor_project.h"
#include "scene/entity_actions.h"
#include "scene/prefab_actions.h"
#include "scene/scene_commands.h"
#include "scene/scene_workspace.h"

#include <spot.h>
#include <spot_import.h>
#include <spot_tools.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using Spot::Editor::BrowserItem;

namespace {

const std::filesystem::path TestAssets = SPOT_TEST_ASSETS_DIR;

void CopyFile(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::filesystem::create_directories(to.parent_path());
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
}

std::vector<std::string> GetNames(const std::vector<BrowserItem>& items) {
    std::vector<std::string> names;
    for (const BrowserItem& item : items) {
        names.push_back(item.name);
    }
    return names;
}

} // namespace

TEST_CASE("The asset browser exposes imported Luau files as script assets", "[editor][assets][luau]") {
    ScriptAssetFixture fixture;
    const auto id = fixture.Import("scripts/hello.luau", "function OnStart() print(\"hello\") end");
    const auto folders = Spot::Editor::ListFolders(fixture.sources);
    const auto items = Spot::Editor::ListFolder(fixture.database, folders, "scripts");
    REQUIRE(items.size() == 1);
    CHECK(items[0].id == id);
    CHECK(items[0].type == Spot::AssetType::Script);
    CHECK(items[0].GetDisplayName() == "hello");
    CHECK_FALSE(items[0].folder);
    CHECK_FALSE(items[0].hasSubAssets);
}

TEST_CASE("The asset browser lists folders first, then assets, by name", "[editor][assets]") {
    const TempDirectory temp;
    const auto sources = temp.GetPath() / "assets";
    CopyFile(TestAssets / "pixels.png", sources / "Zebra.png");
    CopyFile(TestAssets / "pixels.png", sources / "apple.png");
    CopyFile(TestAssets / "models" / "cube.glb", sources / "models" / "cube.glb");
    std::filesystem::create_directories(sources / "Textures" / "UI");
    std::filesystem::create_directories(sources / ".hidden");
    std::ofstream(sources / "notes.txt") << "not an asset";
    Spot::AssetDatabase database(sources, temp.GetPath() / "imported");
    database.Refresh();

    const std::vector<std::string> folders = Spot::Editor::ListFolders(sources);
    CHECK(folders == std::vector<std::string>{"models", "Textures", "Textures/UI"});
    CHECK(Spot::Editor::GetSubfolders(folders, "Textures") == std::vector<std::string>{"Textures/UI"});

    const std::vector<BrowserItem> root = Spot::Editor::ListFolder(database, folders, "");
    CHECK(GetNames(root) == std::vector<std::string>{"models", "Textures", "apple.png", "Zebra.png"});
    CHECK(root[0].folder);
    CHECK(root[0].GetKey() == "folder:models");
    CHECK(root[2].GetKey() == root[2].id.ToString());
    // Files show their names without the extension, since the browser shows their types; folders as they are.
    CHECK(root[2].GetDisplayName() == "apple");
    CHECK(root[0].GetDisplayName() == "models");
    CHECK(root[0].hasContents);
    CHECK(root[1].hasContents);
    CHECK_FALSE(Spot::Editor::ListFolder(database, folders, "Textures")[0].hasContents);

    const std::vector<BrowserItem> models = Spot::Editor::ListFolder(database, folders, "models");
    REQUIRE(models.size() == 1);
    CHECK(models[0].hasSubAssets);
    const std::vector<BrowserItem> parts = Spot::Editor::ListSubAssets(database, models[0].id);
    CHECK(parts.size() == 4);
    CHECK(std::ranges::all_of(parts, &BrowserItem::subAsset));

    // Searches look in every folder, at folders and sub-assets too.
    const std::vector<BrowserItem> found = Spot::Editor::SearchAssets(database, folders, "UI");
    CHECK(GetNames(found) == std::vector<std::string>{"UI"});
    CHECK(GetNames(Spot::Editor::SearchAssets(database, folders, "PNG")) ==
          std::vector<std::string>{"apple.png", "Zebra.png"});
}

TEST_CASE("Asset paths split and join by folder", "[editor][assets]") {
    CHECK(Spot::Editor::GetParentFolder("textures/ui/button.png") == "textures/ui");
    CHECK(Spot::Editor::GetParentFolder("button.png").empty());
    CHECK(Spot::Editor::GetFileName("textures/ui/button.png") == "button.png");
    CHECK(Spot::Editor::JoinPath("", "a.png") == "a.png");
    CHECK(Spot::Editor::JoinPath("textures", "a.png") == "textures/a.png");
    CHECK(Spot::Editor::IsInFolder("textures/ui/a.png", "textures"));
    CHECK(Spot::Editor::IsInFolder("textures", "textures"));
    CHECK_FALSE(Spot::Editor::IsInFolder("textures2/a.png", "textures"));
    CHECK(Spot::Editor::IsInFolder("anything", ""));
}

TEST_CASE("New names must work as file names everywhere", "[editor][assets]") {
    CHECK_FALSE(Spot::Editor::ValidateFileName("Crate 2.png").has_value());
    CHECK(Spot::Editor::ValidateFileName("").has_value());
    CHECK(Spot::Editor::ValidateFileName("   ").has_value());
    CHECK(Spot::Editor::ValidateFileName("a/b").has_value());
    CHECK(Spot::Editor::ValidateFileName("what?").has_value());
    CHECK(Spot::Editor::ValidateFileName(".hidden").has_value());
    CHECK(Spot::Editor::ValidateFileName("name.").has_value());
    CHECK(Spot::Editor::ValidateFileName("CON.png").has_value());
    CHECK(Spot::Editor::ValidateFileName("..").has_value());
}

TEST_CASE("Code shows as plain files, by folder, with their kinds", "[editor][assets]") {
    const TempDirectory temp;
    const auto code = temp.GetPath() / "src";
    std::filesystem::create_directories(code / "player");
    std::filesystem::create_directories(code / "empty");
    std::filesystem::create_directories(code / ".hidden");
    std::ofstream(code / "CMakeLists.txt") << "";
    std::ofstream(code / "game.cpp") << "";
    std::ofstream(code / "player" / "player.h") << "";
    std::ofstream(code / ".clang-format") << "";

    const std::vector<std::string> folders = Spot::Editor::ListFolders(code);
    CHECK(folders == std::vector<std::string>{"empty", "player"});
    const std::vector<BrowserItem> root = Spot::Editor::ListCodeFolder(code, folders, "");
    CHECK(GetNames(root) == std::vector<std::string>{"empty", "player", "CMakeLists.txt", "game.cpp"});
    CHECK(std::ranges::all_of(root,
                              [](const BrowserItem& item) { return item.root == Spot::Editor::BrowserRoot::Code; }));
    CHECK_FALSE(root[0].hasContents);
    CHECK(root[1].hasContents);
    CHECK(root[1].GetKey() == "code:player");
    CHECK(root[3].GetKey() == "code:game.cpp");
    CHECK(root[3].GetDisplayName() == "game");
    CHECK(Spot::Editor::ListCodeFolder(code, folders, "player")[0].path == "player/player.h");

    CHECK(GetNames(Spot::Editor::SearchCode(code, folders, "PLAY")) == std::vector<std::string>{"player", "player.h"});
    CHECK(GetNames(Spot::Editor::SearchCode(code, folders, "game")) == std::vector<std::string>{"game.cpp"});
    CHECK(Spot::Editor::SearchCode(temp.GetPath() / "missing", {}, "game").empty());

    CHECK(Spot::Editor::GetFileTypeName("game.cpp") == "C++ Source");
    CHECK(Spot::Editor::GetFileTypeName("player.HPP") == "C++ Header");
    CHECK(Spot::Editor::GetFileTypeName("CMakeLists.txt") == "CMake Script");
    CHECK(Spot::Editor::GetFileTypeName("tools.cmake") == "CMake Script");
    CHECK(Spot::Editor::GetFileTypeName("notes.md") == "MD File");
    CHECK(Spot::Editor::GetFileTypeName("LICENSE") == "File");
}

TEST_CASE("Plain files move, duplicate, delete and come in like assets, without meta files", "[editor][assets]") {
    const TempDirectory temp;
    Spot::Editor::PlainFiles files(temp.GetPath());
    std::ofstream(temp.GetPath() / "player.cpp") << "// The player\n";

    CHECK(files.GetFreePath("player.cpp") == "player 2.cpp");
    CHECK(files.GetFreePath("enemy.cpp") == "enemy.cpp");
    CHECK(files.CreateFolder("actors/heroes"));
    CHECK(files.Move("player.cpp", "actors/player.cpp"));
    CHECK(std::filesystem::is_regular_file(temp.GetPath() / "actors" / "player.cpp"));
    CHECK_FALSE(files.Move("actors", "actors/heroes/actors"));
    CHECK(files.Duplicate("actors/player.cpp", files.GetFreePath("actors/player.cpp")));
    CHECK(std::filesystem::is_regular_file(temp.GetPath() / "actors" / "player 2.cpp"));
    CHECK_FALSE(files.Move("../outside.cpp", "inside.cpp"));

    const std::vector<std::filesystem::path> dropped{temp.GetPath() / "actors" / "player 2.cpp"};
    CHECK(files.CopyIn(dropped, "") == std::vector<std::string>{"player 2.cpp"});
    CHECK(files.Delete("actors"));
    CHECK_FALSE(std::filesystem::exists(temp.GetPath() / "actors"));
    CHECK(std::filesystem::is_regular_file(temp.GetPath() / "player 2.cpp"));
    CHECK_FALSE(std::filesystem::exists(temp.GetPath() / "player 2.cpp.meta"));
}

namespace {

// A project made from the 3D template, with a texture, browsed headless.
struct BrowserFixture {
    TempDirectory temp;
    std::unique_ptr<Spot::Window> window = Spot::Window::Create();
    Spot::Tests::HeadlessImGui imgui;
    Spot::Editor::EditorSettings settings;
    std::unique_ptr<Spot::Editor::EditorContext> context;
    std::unique_ptr<Spot::Editor::AssetBrowserPanel> panel;
    std::filesystem::path assets;

    BrowserFixture() {
        REQUIRE(window != nullptr);
        const auto file = Spot::CreateProject(temp.GetPath(), "Game", Spot::ProjectTemplate::Basic3D);
        REQUIRE(file.has_value());
        context = std::make_unique<Spot::Editor::EditorContext>(*window, nullptr, settings);
        context->SetProject(Spot::Editor::EditorProject::Open(*file, nullptr));
        REQUIRE(context->GetProject());
        assets = Project().GetProject().GetAssetsDirectory();
        CopyFile(TestAssets / "pixels.png", assets / "pixels.png");
        Project().Refresh();

        panel = std::make_unique<Spot::Editor::AssetBrowserPanel>(*context);
        imgui.SetDraw([this] {
            ImGui::SetNextWindowPos({0.0f, 0.0f});
            ImGui::SetNextWindowSize({900.0f, 500.0f});
            panel->Draw();
        });
        imgui.Frame();
        imgui.Frame();
    }

    Spot::Editor::EditorProject& Project() { return *context->GetProject(); }

    std::string KeyOf(const std::string& path) {
        if (std::filesystem::is_directory(assets / path)) {
            return "folder:" + path;
        }
        const auto id = Project().GetAssetDatabase().FindByPath(path);
        REQUIRE(id.has_value());
        return id->ToString();
    }

    ImVec2 Center(const std::optional<std::pair<ImVec2, ImVec2>>& rect) {
        REQUIRE(rect.has_value());
        return {(rect->first.x + rect->second.x) * 0.5f, (rect->first.y + rect->second.y) * 0.5f};
    }

    ImVec2 Tile(const std::string& path) { return Center(panel->GetTileRect(KeyOf(path))); }

    void DoubleClick(ImVec2 point) {
        imgui.Click(point);
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        imgui.Frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        imgui.Frame();
        imgui.Frame();
    }
};

} // namespace

TEST_CASE("Prefab documents preserve source identities and refuse invalid trees", "[editor][prefab][editing]") {
    BrowserFixture fixture;
    Spot::Scene source;
    auto& root = source.CreateEntity("Lamp");
    root.CreateChild("Bulb");
    const auto componentId = root.AddComponent<Spot::PointLight>().GetId();
    const auto asset = Spot::Editor::CreatePrefab(fixture.Project(), root, "prefabs");
    REQUIRE(asset);
    CHECK(std::filesystem::exists(fixture.assets / "prefabs/Lamp.prefab.meta"));
    Spot::Editor::SceneWorkspace workspace(*fixture.context);
    auto* document = workspace.OpenScene(*asset);
    REQUIRE(document);
    CHECK(document->IsPrefab());
    REQUIRE(document->GetScene().GetEntity(root.GetId()));
    CHECK(document->GetScene().GetEntity(root.GetId())->GetComponent<Spot::PointLight>()->GetId() == componentId);
    auto& edited = *document->GetScene().GetEntity(root.GetId());
    edited.SetName("Edited Lamp");
    REQUIRE(document->Save());
    fixture.Project().Refresh();
    Spot::Scene loaded;
    auto* instance = loaded.Instantiate(fixture.Project().GetAssets().Load<Spot::Prefab>(*asset),
                                        {.assets = &fixture.Project().GetAssets()});
    REQUIRE(instance);
    CHECK(instance->GetName() == "Edited Lamp");
    CHECK(instance->GetPrefabLocalId() == static_cast<std::uint64_t>(root.GetId()));
    CHECK(instance->GetComponent<Spot::PointLight>()->GetId() == componentId);
    const auto before = fixture.Project().GetAssets().Load<Spot::Prefab>(*asset)->GetEntityText();
    auto& invalid = document->GetScene().CreateEntity("Extra root");
    CHECK_FALSE(document->Save());
    document->GetScene().DestroyEntity(invalid);
    auto reopened = Spot::Editor::SceneDocument::Open(document->GetFile(), *asset, &fixture.Project().GetAssets());
    REQUIRE(reopened);
    CHECK(reopened->GetScene().SaveToString(true) == document->GetScene().SaveToString(true));
    CHECK(fixture.Project().GetAssets().Load<Spot::Prefab>(*asset)->GetEntityText() == before);
    CHECK_FALSE(Spot::Editor::CreatePrefab(fixture.Project(), root, "../outside"));
}

TEST_CASE("Prefab Apply Revert and Unpack undo and redo both source and instance changes",
          "[editor][prefab][editing]") {
    BrowserFixture fixture;
    Spot::Scene source;
    auto& sourceRoot = source.CreateEntity("Lamp");
    sourceRoot.AddComponent<Spot::PointLight>().SetIntensity(2.0f);
    const auto asset = Spot::Editor::CreatePrefab(fixture.Project(), sourceRoot, "");
    REQUIRE(asset);
    Spot::Editor::SceneDocument document;
    document.SetAssetManager(&fixture.Project().GetAssets());
    auto* root = document.GetScene().Instantiate(fixture.Project().GetAssets().Load<Spot::Prefab>(*asset),
                                                 {.assets = &fixture.Project().GetAssets()});
    REQUIRE(root);
    const auto id = root->GetId();
    const auto component = root->GetComponent<Spot::PointLight>()->GetId();
    root->GetComponent<Spot::PointLight>()->SetIntensity(8.0f);
    root->SetName("Custom");
    const auto before = document.GetScene().SaveToString();
    const Spot::PrefabOverrideScope scope{root->GetPrefabLocalId(),
                                          "/components/" + component.ToString() + "/data/intensity"};
    REQUIRE(document.Execute(std::make_unique<Spot::Editor::PrefabCommand>(document, fixture.Project(), id,
                                                                           Spot::Editor::PrefabAction::Apply, scope)));
    const auto get = [&]() -> Spot::Entity& { return *document.GetScene().GetEntity(id); };
    CHECK(get().GetComponent<Spot::PointLight>()->GetIntensity() == 8.0f);
    CHECK(get().GetName() == "Custom");
    const auto intensity = [&] {
        Spot::Scene fresh;
        auto* instance = fresh.Instantiate(fixture.Project().GetAssets().Load<Spot::Prefab>(*asset),
                                           {.assets = &fixture.Project().GetAssets()});
        REQUIRE(instance);
        return instance->GetComponent<Spot::PointLight>()->GetIntensity();
    };
    CHECK(intensity() == 8.0f);
    document.Undo();
    CHECK(intensity() == 2.0f);
    CHECK(document.GetScene().SaveToString() == before);
    document.Redo();
    CHECK(intensity() == 8.0f);
    REQUIRE(document.Execute(std::make_unique<Spot::Editor::PrefabCommand>(document, fixture.Project(), id,
                                                                           Spot::Editor::PrefabAction::Revert)));
    CHECK(get().GetName() == "Lamp");
    document.Undo();
    CHECK(get().GetName() == "Custom");
    document.Redo();
    REQUIRE(document.Execute(std::make_unique<Spot::Editor::PrefabCommand>(document, fixture.Project(), id,
                                                                           Spot::Editor::PrefabAction::Unpack)));
    CHECK_FALSE(get().GetPrefabAssetId().IsValid());
    document.Undo();
    CHECK(get().GetPrefabAssetId() == *asset);
    document.Redo();
    CHECK_FALSE(get().GetPrefabAssetId().IsValid());
}

TEST_CASE("Dragging entities into an asset folder creates one prefab per selected root", "[editor][prefab][ui]") {
    BrowserFixture fixture;
    REQUIRE(fixture.Project().GetAssetDatabase().CreateFolder("prefabs"));
    fixture.Project().Refresh();
    Spot::Editor::SceneWorkspace workspace(*fixture.context);
    fixture.context->SetWorkspace(&workspace);
    auto& document = workspace.NewScene();
    auto& root = document.GetScene().CreateEntity("Spawnable");
    auto& child = root.CreateChild("Detail");
    const std::array selected{root.GetId(), child.GetId()};
    document.GetSelection().Set(selected);
    Spot::Editor::HierarchyPanel hierarchy(*fixture.context);
    fixture.imgui.SetDraw([&] {
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({800.0f, 400.0f});
        fixture.panel->Draw();
        ImGui::SetNextWindowPos({0.0f, 420.0f});
        ImGui::SetNextWindowSize({800.0f, 280.0f});
        hierarchy.Draw();
    });
    fixture.imgui.Frame();
    fixture.imgui.Frame();
    const auto row = hierarchy.GetRowRect(root.GetId());
    REQUIRE(row);
    fixture.imgui.Drag({row->first.x + 60.0f, (row->first.y + row->second.y) * 0.5f}, fixture.Tile("prefabs"));
    const auto id = fixture.Project().GetAssetDatabase().FindByPath("prefabs/Spawnable.prefab");
    REQUIRE(id);
    CHECK_FALSE(fixture.Project().GetAssetDatabase().FindByPath("prefabs/Detail.prefab"));
    CHECK_FALSE(root.GetPrefabAssetId().IsValid());
    fixture.panel->SetFolder("prefabs");
    fixture.imgui.Frame();
    fixture.DoubleClick(fixture.Tile("prefabs/Spawnable.prefab"));
    REQUIRE(workspace.GetActiveDocument());
    CHECK(workspace.GetActiveDocument()->IsPrefab());
    CHECK(workspace.GetActiveDocument()->GetAssetId() == *id);
    fixture.context->SetWorkspace(nullptr);
}

TEST_CASE("The prefab inspector applies reverts and unpacks through its instance controls", "[editor][prefab][ui]") {
    BrowserFixture fixture;
    Spot::Scene source;
    auto& sourceRoot = source.CreateEntity("Lamp");
    sourceRoot.AddComponent<Spot::PointLight>();
    const auto id = Spot::Editor::CreatePrefab(fixture.Project(), sourceRoot, "");
    REQUIRE(id);
    Spot::Editor::SceneWorkspace workspace(*fixture.context);
    fixture.context->SetWorkspace(&workspace);
    auto& document = workspace.NewScene();
    auto* root = document.GetScene().Instantiate(fixture.Project().GetAssets().Load<Spot::Prefab>(*id),
                                                 {.assets = &fixture.Project().GetAssets()});
    REQUIRE(root);
    const auto rootId = root->GetId();
    root->SetName("Custom");
    document.GetSelection().Set(std::array{rootId});
    Spot::Editor::InspectorPanel inspector(*fixture.context);
    fixture.imgui.SetDraw([&] {
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({700.0f, 700.0f});
        inspector.Draw();
    });
    fixture.imgui.Frame();
    fixture.imgui.Frame();
    auto& panel = inspector;
    fixture.imgui.Click(fixture.Center(panel.GetItemRect("Prefab/Revert All")));
    REQUIRE(document.GetScene().GetEntity(rootId));
    CHECK(document.GetScene().GetEntity(rootId)->GetName() == "Lamp");
    document.Undo();
    fixture.imgui.Frame();
    const auto light = [&]() -> Spot::PointLight& {
        return *document.GetScene().GetEntity(rootId)->GetComponent<Spot::PointLight>();
    };
    const float original = light().GetIntensity();
    light().SetIntensity(original + 4.0f);
    fixture.imgui.Frame();
    fixture.imgui.Click(fixture.Center(panel.GetItemRect("PointLight/data/intensity")), 0, ImGuiMouseButton_Right);
    fixture.imgui.Click(fixture.Center(panel.GetItemRect("PointLight/data/intensity/menu/Revert Override")));
    CHECK(light().GetIntensity() == original);
    document.Undo();
    fixture.imgui.Frame();
    CHECK(light().GetIntensity() == original + 4.0f);
    fixture.imgui.Click(fixture.Center(panel.GetItemRect("PointLight/menu")));
    fixture.imgui.Click(fixture.Center(panel.GetItemRect("PointLight/menu/Apply Component to Prefab")));
    CHECK(light().GetIntensity() == original + 4.0f);
    CHECK(Spot::GetPrefabOverrides(*document.GetScene().GetEntity(rootId)).size() == 1);
    fixture.imgui.Click(fixture.Center(panel.GetItemRect("Prefab/Apply All")));
    CHECK(Spot::GetPrefabOverrides(*document.GetScene().GetEntity(rootId)).empty());
    fixture.imgui.Click(fixture.Center(panel.GetItemRect("Prefab/Unpack")));
    CHECK_FALSE(document.GetScene().GetEntity(rootId)->GetPrefabAssetId().IsValid());
    document.Undo();
    CHECK(document.GetScene().GetEntity(rootId)->GetPrefabAssetId() == *id);
    fixture.context->SetWorkspace(nullptr);
}

TEST_CASE("Folders open with a double click, and Backspace goes back up", "[editor][assets][ui]") {
    BrowserFixture fixture;
    fixture.DoubleClick(fixture.Tile("scenes"));
    CHECK(fixture.panel->GetFolder() == "scenes");
    CHECK(fixture.panel->GetTileRect(fixture.KeyOf("scenes/Main.scene")).has_value());

    fixture.imgui.Press(ImGuiKey_Backspace);
    CHECK(fixture.panel->GetFolder().empty());
    // The folder just left is selected.
    CHECK(fixture.panel->GetSelection() == std::vector<std::string>{"folder:scenes"});
}

TEST_CASE("Prefab authoring preserves nested links and creates variants from linked instance roots",
          "[editor][prefab][nesting][variants]") {
    BrowserFixture fixture;
    // Imported prefabs have their nested prefabs expanded; sources keep them.
    const auto sourceOf = [&](Spot::AssetId id) {
        const Spot::AssetInfo* info = fixture.Project().GetAssetDatabase().Find(id);
        REQUIRE(info);
        std::ifstream input(fixture.assets / Spot::Utf8ToPath(info->path), std::ios::binary);
        const auto prefab = Spot::Prefab::FromString(
            std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()});
        REQUIRE(prefab);
        return *prefab;
    };
    Spot::Scene source;
    auto& baseRoot = source.CreateEntity("Lamp");
    baseRoot.AddComponent<Spot::PointLight>().SetIntensity(2.0f);
    const auto base = Spot::Editor::CreatePrefab(fixture.Project(), baseRoot, "");
    REQUIRE(base);
    Spot::Scene authored;
    auto& assemblyRoot = authored.CreateEntity("Assembly");
    auto* lamp = authored.Instantiate(fixture.Project().GetAssets().Load<Spot::Prefab>(*base),
                                      {.parent = &assemblyRoot, .assets = &fixture.Project().GetAssets()});
    REQUIRE(lamp);
    const auto assembly = Spot::Editor::CreatePrefab(fixture.Project(), assemblyRoot, "");
    REQUIRE(assembly);
    CHECK(sourceOf(*assembly).GetDependencies().size() == 1);
    CHECK(fixture.Project().GetAssets().Load<Spot::Prefab>(*assembly)->IsFlat());
    lamp->SetName("Variant");
    const auto variant = Spot::Editor::CreatePrefab(fixture.Project(), *lamp, "");
    REQUIRE(variant);
    CHECK(sourceOf(*variant).GetDependencies()[0] == *base);
    Spot::Editor::SceneWorkspace workspace(*fixture.context);
    auto* document = workspace.OpenScene(*variant);
    REQUIRE(document);
    REQUIRE(document->IsPrefab());
    CHECK(document->GetScene().GetRootEntities()[0]->GetPrefabAssetId() == *base);
    document->GetScene().GetRootEntities()[0]->GetComponent<Spot::PointLight>()->SetIntensity(8.0f);
    REQUIRE(document->Save());
    fixture.Project().Refresh();
    CHECK(sourceOf(*variant).GetDependencies()[0] == *base);
    Spot::Scene loaded;
    auto* instance = loaded.Instantiate(fixture.Project().GetAssets().Load<Spot::Prefab>(*variant),
                                        {.assets = &fixture.Project().GetAssets()});
    REQUIRE(instance);
    CHECK(instance->GetName() == "Variant");
    CHECK(instance->GetComponent<Spot::PointLight>()->GetIntensity() == 8.0f);
    CHECK(baseRoot.GetComponent<Spot::PointLight>()->GetIntensity() == 2.0f);
    auto* assemblyDocument = workspace.OpenScene(*assembly);
    REQUIRE(assemblyDocument);
    REQUIRE(assemblyDocument->GetScene().GetRootEntities()[0]->FindChild("Lamp"));
    CHECK(assemblyDocument->GetScene().GetRootEntities()[0]->FindChild("Lamp")->GetPrefabAssetId() == *base);
    REQUIRE(assemblyDocument->Save());
    fixture.Project().Refresh();
    CHECK(sourceOf(*assembly).GetDependencies()[0] == *base);
}

TEST_CASE("Tiles out of view draw nothing", "[editor][assets][ui]") {
    BrowserFixture fixture;
    const auto addMaterials = [&](int from, int to) {
        for (int i = from; i < to; ++i) {
            REQUIRE(
                Spot::MaterialSource{}.SaveToFile(fixture.assets / "many" / std::format("Material {:03}.material", i)));
        }
        fixture.Project().Refresh();
        fixture.imgui.Frame();
        fixture.imgui.Frame();
        return ImGui::GetDrawData()->TotalVtxCount;
    };
    std::filesystem::create_directories(fixture.assets / "many");
    fixture.panel->SetFolder("many");
    const int few = addMaterials(0, 100);
    // Only the first rows fit in the panel, so twice as many materials take about as many vertices.
    const int many = addMaterials(100, 200);
    CHECK(many < few + few / 10);
}

TEST_CASE("New assets are created and named in place", "[editor][assets][ui]") {
    BrowserFixture fixture;
    fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("Create")));
    fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("Create/Material")));
    CHECK(std::filesystem::exists(fixture.assets / "New Material.material"));

    // The name is typed right away; the extension stays.
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_A);
    fixture.imgui.Type("Bricks");
    fixture.imgui.Press(ImGuiKey_Enter);
    CHECK(std::filesystem::exists(fixture.assets / "Bricks.material"));
    CHECK_FALSE(std::filesystem::exists(fixture.assets / "New Material.material"));
    CHECK(fixture.panel->GetSelection() == std::vector<std::string>{fixture.KeyOf("Bricks.material")});
}

TEST_CASE("The asset browser numbers new scripts and keeps their code when they are renamed",
          "[editor][assets][ui][luau]") {
    BrowserFixture fixture;
    REQUIRE(fixture.Project().GetAssetDatabase().CreateFolder("scripts"));
    fixture.Project().Refresh();
    fixture.panel->SetFolder("scripts");
    fixture.imgui.Frame();
    const auto create = [&] {
        fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("Create")));
        fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("Create/Luau Script")));
    };
    create();
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_A);
    fixture.imgui.Type("Player");
    fixture.imgui.Press(ImGuiKey_Enter);
    const auto id = fixture.Project().GetAssetDatabase().FindByPath("scripts/Player.luau");
    REQUIRE(id);
    CHECK(fixture.panel->GetSelection() == std::vector<std::string>{id->ToString()});
    const auto renamed = fixture.Project().GetAssets().Load<Spot::ScriptSource>(*id);
    REQUIRE(renamed);
    create();
    fixture.imgui.Press(ImGuiKey_Enter);
    create();
    fixture.imgui.Press(ImGuiKey_Enter);
    const auto first = fixture.Project().GetAssetDatabase().FindByPath("scripts/New Script.luau");
    const auto second = fixture.Project().GetAssetDatabase().FindByPath("scripts/New Script 2.luau");
    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first != *second);
    const auto numbered = fixture.Project().GetAssets().Load<Spot::ScriptSource>(*second);
    REQUIRE(numbered);
    CHECK(numbered->source.starts_with("-- New Script 2.luau\n\n"));

    // Once its code is the user's, renaming a script leaves it as it is.
    const std::string edited = renamed->source + "-- User code\nlocal function helper() return 42 end\n";
    {
        std::ofstream file(fixture.assets / "scripts/Player.luau", std::ios::binary);
        file << edited;
        REQUIRE(file.good());
    }
    fixture.Project().Refresh();
    fixture.imgui.Frame();
    fixture.imgui.Click(fixture.Tile("scripts/Player.luau"));
    fixture.imgui.Press(ImGuiKey_F2);
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_A);
    fixture.imgui.Type("Actor");
    fixture.imgui.Press(ImGuiKey_Enter);
    CHECK(fixture.Project().GetAssetDatabase().FindByPath("scripts/Actor.luau") == id);
    const auto customized = fixture.Project().GetAssets().Load<Spot::ScriptSource>(*id);
    REQUIRE(customized);
    CHECK(customized->source == edited);
}

TEST_CASE("The asset browser creates Luau script templates and opens them in the code editor",
          "[editor][assets][ui][luau]") {
    BrowserFixture fixture;
    REQUIRE(fixture.Project().GetAssetDatabase().CreateFolder("scripts"));
    fixture.Project().Refresh();
    fixture.panel->SetFolder("scripts");
    fixture.imgui.Frame();
    fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("Create")));
    fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("Create/Luau Script")));
    const auto id = fixture.Project().GetAssetDatabase().FindByPath("scripts/New Script.luau");
    REQUIRE(id);
    REQUIRE(std::filesystem::is_regular_file(fixture.assets / "scripts/New Script.luau.meta"));
    const auto script = fixture.Project().GetAssets().Load<Spot::ScriptSource>(*id);
    REQUIRE(script);
    CHECK(script->source.starts_with("-- New Script.luau\n\nlocal spot = require(\"@spot\")\n\n"));
    CHECK(script->source.find("function OnStart()\n    \nend") != std::string::npos);
    CHECK(script->source.find("function OnUpdate(deltaTime)\n    \nend") != std::string::npos);
#ifdef SPOT_ENABLE_LUAU
    Spot::Scene scene;
    auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    component.script = script;
    scene.Update(0.016f);
    CHECK_FALSE(component.IsFaulted());
#endif
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_A);
    fixture.imgui.Type("Player");
    fixture.imgui.Press(ImGuiKey_Enter);
    CHECK(fixture.Project().GetAssetDatabase().FindByPath("scripts/Player.luau") == id);
    const auto renamed = fixture.Project().GetAssets().Load<Spot::ScriptSource>(*id);
    REQUIRE(renamed);
    CHECK(renamed->source.starts_with("-- Player.luau\n\n"));

    const ImVec4 luauColor = Spot::Editor::AssetIcons::GetTypeColor(Spot::AssetType::Script, "scripts/Player.luau");
    CHECK(luauColor.z > luauColor.x);
    CHECK(luauColor.z > luauColor.y);

    Spot::Editor::CodeEditorPanel code(*fixture.context);
    fixture.context->SetCodeEditor(&code);
    fixture.imgui.Frame();
    fixture.DoubleClick(fixture.Tile("scripts/Player.luau"));
    CHECK(code.GetDocumentCount() == 1);
    CHECK(code.IsOpen());
    CHECK(code.GetActivePath() == "scripts/Player.luau");
    REQUIRE(code.GetActiveEditor());
    CHECK(code.GetActiveEditor()->GetLanguageName() == "Luau");
    fixture.context->SetCodeEditor(nullptr);
}

TEST_CASE("The asset browser creates a sprite atlas from the selected texture", "[editor][sprites][ui]") {
    BrowserFixture fixture;
    fixture.imgui.Click(fixture.Tile("pixels.png"));
    fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("Create")));
    fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("Create/Sprite Atlas")));
    CHECK(std::filesystem::is_regular_file(fixture.assets / "New Atlas.atlas.meta"));
    const auto id = fixture.Project().GetAssetDatabase().FindByPath("New Atlas.atlas");
    REQUIRE(id);
    const auto atlas = fixture.Project().GetAssets().Load<Spot::SpriteAtlas>(*id);
    REQUIRE(atlas);
    CHECK(atlas->texture.GetId() == *fixture.Project().GetAssetDatabase().FindByPath("pixels.png"));
    CHECK(atlas->animations.front().name == "Idle");
    CHECK(atlas->sprites.front().size == atlas->textureSize);
    fixture.imgui.Press(ImGuiKey_Enter);
    Spot::Editor::AssetInspector inspector(*fixture.context);
    fixture.imgui.SetDraw([&] {
        ImGui::SetNextWindowSize({900, 700});
        ImGui::Begin("Atlas");
        inspector.Draw(fixture.Project(), *id);
        ImGui::End();
    });
    fixture.imgui.Frame();
    fixture.imgui.Frame();
    CHECK(inspector.GetItemRect("/texture").has_value());
    CHECK(inspector.GetItemRect("Atlas/Apply").has_value());
    REQUIRE(inspector.GetHistory());
    CHECK(inspector.GetHistory()->GetUndoCount() == 0);
}

TEST_CASE("F2 renames, keeping the asset's id, and Ctrl+D duplicates", "[editor][assets][ui]") {
    BrowserFixture fixture;
    const std::string key = fixture.KeyOf("pixels.png");
    fixture.imgui.Click(fixture.Tile("pixels.png"));
    fixture.imgui.Press(ImGuiKey_F2);
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_A);
    fixture.imgui.Type("crate");
    fixture.imgui.Press(ImGuiKey_Enter);
    CHECK(fixture.KeyOf("crate.png") == key);

    // A name that is taken keeps the field open.
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_D);
    CHECK(std::filesystem::exists(fixture.assets / "crate 2.png"));
    CHECK(fixture.KeyOf("crate 2.png") != key);
    fixture.imgui.Press(ImGuiKey_F2);
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_A);
    fixture.imgui.Type("crate");
    fixture.imgui.Press(ImGuiKey_Enter);
    CHECK(std::filesystem::exists(fixture.assets / "crate 2.png"));
    fixture.imgui.Press(ImGuiKey_Escape);
    CHECK(std::filesystem::exists(fixture.assets / "crate 2.png"));
}

TEST_CASE("Deleting asks first", "[editor][assets][ui]") {
    BrowserFixture fixture;
    fixture.imgui.Click(fixture.Tile("pixels.png"));
    fixture.imgui.Press(ImGuiKey_Delete);
    fixture.imgui.Press(ImGuiKey_Escape);
    CHECK(std::filesystem::exists(fixture.assets / "pixels.png"));

    fixture.imgui.Press(ImGuiKey_Delete);
    fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("DeletePrompt/Delete")));
    CHECK_FALSE(std::filesystem::exists(fixture.assets / "pixels.png"));
    CHECK_FALSE(std::filesystem::exists(fixture.assets / "pixels.png.meta"));
    CHECK(fixture.panel->GetSelection().empty());
}

TEST_CASE("Dragging assets onto a folder moves them there", "[editor][assets][ui]") {
    BrowserFixture fixture;
    const std::string key = fixture.KeyOf("pixels.png");
    fixture.imgui.Drag(fixture.Tile("pixels.png"), fixture.Tile("scenes"));
    CHECK_FALSE(std::filesystem::exists(fixture.assets / "pixels.png"));
    CHECK(fixture.KeyOf("scenes/pixels.png") == key);
}

TEST_CASE("Files dropped from the OS are imported into the folder shown", "[editor][assets][ui]") {
    BrowserFixture fixture;
    fixture.DoubleClick(fixture.Tile("scenes"));
    fixture.panel->DropFile(TestAssets / "sine.ogg", {-100.0f, -100.0f});
    fixture.imgui.Frame();
    REQUIRE(std::filesystem::exists(fixture.assets / "scenes" / "sine.ogg"));
    CHECK(fixture.panel->GetSelection() == std::vector<std::string>{fixture.KeyOf("scenes/sine.ogg")});
}

TEST_CASE("Meshes dropped into scenes become entities, and materials go to meshes", "[editor][assets]") {
    BrowserFixture fixture;
    CopyFile(TestAssets / "models" / "cube.glb", fixture.assets / "models" / "cube.glb");
    REQUIRE(Spot::MaterialSource{}.SaveToFile(fixture.assets / "Paint.material"));
    fixture.Project().Refresh();
    const Spot::AssetDatabase& database = fixture.Project().GetAssetDatabase();
    const auto material = database.FindByPath("Paint.material");
    REQUIRE(material.has_value());
    const auto mesh = std::ranges::find(database.GetAssets(), Spot::AssetType::Mesh, &Spot::AssetInfo::type);
    REQUIRE(mesh != database.GetAssets().end());

    Spot::Editor::SceneDocument document;
    REQUIRE(Spot::Editor::CreateEntityFromAsset(document, fixture.Project(), mesh->id, Spot::EntityId::Invalid,
                                                {1.0f, 0.0f, 2.0f}));
    Spot::Entity* entity = document.GetScene().GetEntity(document.GetSelection().GetPrimary());
    REQUIRE(entity);
    CHECK(entity->GetName() == mesh->name);
    CHECK(entity->GetTransform().GetPosition() == Spot::Vector3{1.0f, 0.0f, 2.0f});
    REQUIRE(entity->GetComponent<Spot::MeshRenderer>());
    CHECK(entity->GetComponent<Spot::MeshRenderer>()->GetMesh().GetId() == mesh->id);
    // Materials apply to existing entities; textures make sprites.
    CHECK_FALSE(Spot::Editor::CreateEntityFromAsset(document, fixture.Project(), *material));
    const auto texture = database.FindByPath("pixels.png");
    REQUIRE(Spot::Editor::CreateEntityFromAsset(document, fixture.Project(), *texture));
    const Spot::Entity* sprite = document.GetScene().GetEntity(document.GetSelection().GetPrimary());
    REQUIRE((sprite && sprite->GetComponent<Spot::SpriteRenderer>()));
    CHECK(sprite->GetName() == "pixels");
    CHECK(sprite->GetComponent<Spot::SpriteRenderer>()->GetTexture().GetId() == *texture);

    REQUIRE(Spot::Editor::ApplyMaterial(document, entity->GetId(), *material));
    CHECK(entity->GetComponent<Spot::MeshRenderer>()->GetMaterial().GetId() == *material);
    CHECK(document.GetHistory().GetUndoName() == "Apply Material");
    document.Undo();
    CHECK_FALSE(entity->GetComponent<Spot::MeshRenderer>()->GetMaterial().GetId().IsValid());
}

TEST_CASE("Model drops preserve hierarchy through undo and redo without a GPU", "[editor][assets][model]") {
    const TempDirectory temp;
    const auto file = Spot::CreateProject(temp.GetPath(), "Game", Spot::ProjectTemplate::Empty);
    REQUIRE(file);
    const auto sources = file->parent_path() / "assets";
    std::string source;
    SECTION("Mesh nodes") {
        source = "cube.glb";
        CopyFile(TestAssets / "models" / source, sources / source);
    }
    SECTION("Empty nodes") {
        source = "nodes.gltf";
        std::ofstream(sources / source) << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
            "nodes":[{"name":"Pivot","translation":[10,0,0],"children":[1]},
                     {"name":"Cube","translation":[1,0,0]}]})";
    }
    const auto project = Spot::Editor::EditorProject::Open(*file, nullptr);
    REQUIRE(project);
    Spot::Editor::SceneDocument document;
    Spot::Entity& parent = document.GetScene().CreateEntity("Parent");
    parent.GetTransform().SetPosition({-5.0f, 0.0f, 0.0f});
    const Spot::EntityId parentId = parent.GetId();
    const std::string before = document.GetScene().SaveToString();
    const auto asset = project->GetAssetDatabase().FindByPath(source);
    REQUIRE(asset);
    REQUIRE(Spot::Editor::CreateEntityFromAsset(document, *project, *asset, parentId, {1.0f, 0.0f, 2.0f}));
    const Spot::EntityId rootId = document.GetSelection().GetPrimary();
    const auto* root = document.GetScene().GetEntity(rootId);
    REQUIRE(root);
    CHECK(root->GetParent()->GetId() == parentId);
    REQUIRE(root->FindChild("Pivot"));
    const auto* cube = root->FindChild("Pivot")->FindChild("Cube");
    REQUIRE(cube);
    CHECK(Spot::Distance(cube->GetTransform().GetWorldPosition(), {7.0f, 0.0f, 2.0f}) < 1e-4f);
    if (source == "cube.glb") {
        const auto renderers = cube->GetComponents<Spot::MeshRenderer>();
        REQUIRE(renderers.size() == 2);
        CHECK(renderers[0]->GetMesh().GetId() == renderers[1]->GetMesh().GetId());
        CHECK(renderers[0]->GetSubmesh() == 0);
        CHECK(renderers[1]->GetSubmesh() == 1);
    } else {
        CHECK_FALSE(cube->HasComponent<Spot::MeshRenderer>());
    }
    const std::string after = document.GetScene().SaveToString();
    document.Undo();
    CHECK(document.GetScene().SaveToString() == before);
    CHECK_FALSE(document.GetScene().GetEntity(rootId));
    document.Redo();
    CHECK(document.GetScene().SaveToString() == after);
    CHECK(document.GetSelection().GetPrimary() == rootId);
}

TEST_CASE("Prefab drops preserve the tree through undo and redo without a GPU", "[editor][assets][prefab]") {
    const TempDirectory temp;
    const auto file = Spot::CreateProject(temp.GetPath(), "Game", Spot::ProjectTemplate::Empty);
    REQUIRE(file);
    const auto sources = file->parent_path() / "assets";
    Spot::Scene source;
    Spot::Entity& root = source.CreateEntity("Lamp");
    root.GetTransform().SetScale({2.0f, 2.0f, 2.0f});
    root.CreateChild("Bulb").AddComponent<Spot::PointLight>();
    std::ofstream(sources / "Lamp.prefab") << root.SaveToString();
    const auto project = Spot::Editor::EditorProject::Open(*file, nullptr);
    REQUIRE(project);
    const auto asset = project->GetAssetDatabase().FindByPath("Lamp.prefab");
    REQUIRE(asset);
    const auto items = Spot::Editor::ListFolder(project->GetAssetDatabase(), {}, "");
    CHECK(std::ranges::any_of(
        items, [&](const BrowserItem& item) { return item.id == *asset && item.type == Spot::AssetType::Prefab; }));
    Spot::Editor::SceneDocument document;
    const auto parentId = document.GetScene().CreateEntity("Parent").GetId();
    const auto before = document.GetScene().SaveToString();
    REQUIRE(Spot::Editor::CreateEntityFromAsset(document, *project, *asset, parentId, {1.0f, 0.0f, 2.0f}));
    const auto rootId = document.GetSelection().GetPrimary();
    const auto* instance = document.GetScene().GetEntity(rootId);
    REQUIRE(instance);
    CHECK(instance->GetName() == "Lamp");
    CHECK(instance->GetPrefabAssetId() == *asset);
    REQUIRE(instance->GetParent());
    CHECK(instance->GetParent()->GetId() == parentId);
    CHECK(instance->GetTransform().GetPosition() == Spot::Vector3{1.0f, 0.0f, 2.0f});
    CHECK(instance->GetTransform().GetScale() == Spot::Vector3{2.0f, 2.0f, 2.0f});
    REQUIRE(instance->FindChild("Bulb"));
    CHECK(instance->FindChild("Bulb")->HasComponent<Spot::PointLight>());
    const auto after = document.GetScene().SaveToString();
    document.Undo();
    CHECK(document.GetScene().SaveToString() == before);
    CHECK_FALSE(document.GetScene().GetEntity(rootId));
    document.Redo();
    CHECK(document.GetScene().SaveToString() == after);
    CHECK(document.GetSelection().GetPrimary() == rootId);
}

TEST_CASE("Materials dragged from the browser onto the hierarchy go to the entity's meshes", "[editor][assets][ui]") {
    BrowserFixture fixture;
    REQUIRE(Spot::MaterialSource{}.SaveToFile(fixture.assets / "Paint.material"));
    fixture.Project().Refresh();
    Spot::Editor::SceneWorkspace workspace(*fixture.context);
    fixture.context->SetWorkspace(&workspace);
    workspace.RestoreState();
    Spot::Editor::SceneDocument& document = *workspace.GetActiveDocument();
    Spot::Editor::HierarchyPanel hierarchy(*fixture.context);
    fixture.imgui.SetDraw([&] {
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({900.0f, 400.0f});
        fixture.panel->Draw();
        ImGui::SetNextWindowPos({0.0f, 420.0f});
        ImGui::SetNextWindowSize({400.0f, 280.0f});
        hierarchy.Draw();
    });
    fixture.imgui.Frame();
    fixture.imgui.Frame();

    Spot::Entity* cube = document.GetScene().FindEntity("Cube");
    REQUIRE(cube);
    const auto row = hierarchy.GetRowRect(cube->GetId());
    REQUIRE(row.has_value());
    fixture.imgui.Drag(fixture.Tile("Paint.material"), {row->first.x + 60.0f, (row->first.y + row->second.y) * 0.5f});
    CHECK(cube->GetComponent<Spot::MeshRenderer>()->GetMaterial().GetId() ==
          fixture.Project().GetAssetDatabase().FindByPath("Paint.material"));
    fixture.context->SetWorkspace(nullptr);
}

TEST_CASE("The inspector edits materials, saving each change as one undoable step", "[editor][assets][ui]") {
    BrowserFixture fixture;
    const auto file = fixture.assets / "Paint.material";
    REQUIRE(Spot::MaterialSource{.roughness = 0.25f}.SaveToFile(file));
    fixture.Project().Refresh();
    Spot::Editor::InspectorPanel inspector(*fixture.context);
    fixture.imgui.SetDraw([&] {
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({800.0f, 400.0f});
        fixture.panel->Draw();
        ImGui::SetNextWindowPos({820.0f, 0.0f});
        ImGui::SetNextWindowSize({440.0f, 700.0f});
        inspector.Draw();
    });
    fixture.imgui.Frame();
    fixture.imgui.Frame();

    fixture.imgui.Click(fixture.Tile("Paint.material"));
    Spot::Editor::AssetInspector& assets = inspector.GetAssetInspector();
    const ImVec2 field = fixture.Center(assets.GetItemRect("/roughness"));
    fixture.imgui.Drag(field, {field.x + 60.0f, field.y});
    const auto saved = Spot::MaterialSource::LoadFromFile(file);
    REQUIRE(saved.has_value());
    CHECK(saved->roughness > 0.25f);
    REQUIRE(assets.GetHistory());
    CHECK(assets.GetHistory()->GetUndoCount() == 1);

    assets.GetHistory()->Undo();
    CHECK(Spot::MaterialSource::LoadFromFile(file)->roughness == 0.25f);
}

TEST_CASE("Import settings apply when asked, and revert", "[editor][assets][ui]") {
    BrowserFixture fixture;
    Spot::Editor::InspectorPanel inspector(*fixture.context);
    fixture.imgui.SetDraw([&] {
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({800.0f, 400.0f});
        fixture.panel->Draw();
        ImGui::SetNextWindowPos({820.0f, 0.0f});
        ImGui::SetNextWindowSize({440.0f, 700.0f});
        inspector.Draw();
    });
    fixture.imgui.Frame();
    fixture.imgui.Click(fixture.Tile("pixels.png"));
    Spot::Editor::AssetInspector& assets = inspector.GetAssetInspector();
    const Spot::AssetId texture = *fixture.Project().GetAssetDatabase().FindByPath("pixels.png");

    fixture.imgui.Click(fixture.Center(assets.GetItemRect("/mipmaps")));
    CHECK(fixture.Project().GetAssetDatabase().GetMeta(texture)->texture.mipmaps);
    fixture.imgui.Click(fixture.Center(assets.GetItemRect("Revert")));
    fixture.imgui.Click(fixture.Center(assets.GetItemRect("Apply")));
    CHECK(fixture.Project().GetAssetDatabase().GetMeta(texture)->texture.mipmaps);

    fixture.imgui.Click(fixture.Center(assets.GetItemRect("/mipmaps")));
    fixture.imgui.Click(fixture.Center(assets.GetItemRect("Apply")));
    CHECK_FALSE(fixture.Project().GetAssetDatabase().GetMeta(texture)->texture.mipmaps);
    CHECK_FALSE(Spot::AssetMeta::LoadFromFile(fixture.assets / "pixels.png.meta")->texture.mipmaps);
}

TEST_CASE("The code shows beside the assets, where its files are handled as files", "[editor][assets][ui]") {
    BrowserFixture fixture;
    // Every project has code, if only what runs its game.
    CHECK(fixture.panel->GetItemRect("Tree/Code").has_value());

    const auto code = fixture.Project().GetProject().GetCodeDirectory();
    std::filesystem::create_directories(code / "player");
    std::ofstream(code / "game.cpp") << "// The game\n";
    fixture.imgui.Frame();
    fixture.imgui.Frame();
    fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("Tree/Code")));
    CHECK(fixture.panel->GetRoot() == Spot::Editor::BrowserRoot::Code);
    CHECK(fixture.panel->GetFolder().empty());
    REQUIRE(fixture.panel->GetTileRect("code:game.cpp").has_value());

    // Files move into code folders, and keep their extension when renamed.
    fixture.imgui.Drag(fixture.Center(fixture.panel->GetTileRect("code:game.cpp")),
                       fixture.Center(fixture.panel->GetTileRect("code:player")));
    CHECK(std::filesystem::is_regular_file(code / "player" / "game.cpp"));
    fixture.DoubleClick(fixture.Center(fixture.panel->GetTileRect("code:player")));
    CHECK(fixture.panel->GetFolder() == "player");
    fixture.imgui.Click(fixture.Center(fixture.panel->GetTileRect("code:player/game.cpp")));
    fixture.imgui.Press(ImGuiKey_F2);
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_A);
    fixture.imgui.Type("player");
    fixture.imgui.Press(ImGuiKey_Enter);
    CHECK(std::filesystem::is_regular_file(code / "player" / "player.cpp"));
    CHECK_FALSE(std::filesystem::exists(code / "player" / "player.cpp.meta"));
    CHECK(fixture.panel->GetSelection() == std::vector<std::string>{"code:player/player.cpp"});

    fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("Tree/Assets")));
    CHECK(fixture.panel->GetRoot() == Spot::Editor::BrowserRoot::Assets);
    CHECK(fixture.panel->GetTileRect(fixture.KeyOf("scenes")).has_value());

    // Code stays out of the assets.
    std::ofstream(code / "scenery.cpp") << "";
    fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("Tree/Code")));
    fixture.imgui.Frame();
    REQUIRE(fixture.panel->GetTileRect("code:scenery.cpp").has_value());
    fixture.imgui.Drag(fixture.Center(fixture.panel->GetTileRect("code:scenery.cpp")),
                       fixture.Center(fixture.panel->GetItemRect("Tree/Assets")));
    CHECK(std::filesystem::is_regular_file(code / "scenery.cpp"));
    CHECK_FALSE(std::filesystem::exists(fixture.assets / "scenery.cpp"));

    // Code has no assets to create, only folders.
    fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("Create")));
    CHECK(fixture.panel->GetItemRect("Create/Folder").has_value());
    CHECK_FALSE(fixture.panel->GetItemRect("Create/Material").has_value());
}

TEST_CASE("C++ components are created from the browser, named first", "[editor][assets][ui]") {
    BrowserFixture fixture;
    fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("Create")));
    fixture.imgui.Click(fixture.Center(fixture.panel->GetItemRect("Create/C++ Component")));
    fixture.imgui.Frame();
    REQUIRE(fixture.panel->GetItemRect("NewComponent/Name").has_value());

    // Names that cannot be leave nothing to create.
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_A);
    fixture.imgui.Type("Camera");
    fixture.imgui.Press(ImGuiKey_Enter);
    const auto code = fixture.Project().GetProject().GetCodeDirectory();
    CHECK_FALSE(std::filesystem::exists(code / "Camera.h"));
    REQUIRE(fixture.panel->GetItemRect("NewComponent/Name").has_value());

    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_A);
    fixture.imgui.Type("Player");
    fixture.imgui.Press(ImGuiKey_Enter);
    CHECK(std::filesystem::is_regular_file(code / "Player.h"));
    CHECK(std::filesystem::is_regular_file(code / "Player.cpp"));
    CHECK(std::filesystem::is_regular_file(code / "CMakeLists.txt"));
    // The browser goes to the new code, selected.
    CHECK(fixture.panel->GetRoot() == Spot::Editor::BrowserRoot::Code);
    CHECK(fixture.panel->GetSelection() == std::vector<std::string>{"code:Player.h", "code:Player.cpp"});
    CHECK(fixture.Project().GetModules().HasCode());
}
