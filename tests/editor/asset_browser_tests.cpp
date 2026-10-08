#include "headless_imgui.h"
#include "temp_directory.h"

#include "app/editor_context.h"
#include "assets/asset_browser_model.h"
#include "panels/asset_browser_panel.h"
#include "panels/hierarchy_panel.h"
#include "panels/inspector_panel.h"
#include "project/editor_project.h"
#include "project/project_templates.h"
#include "scene/entity_actions.h"
#include "scene/scene_workspace.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
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
    CHECK(parts.size() == 5);
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
        const auto file = Spot::Editor::CreateProject(temp.GetPath(), "Game", Spot::Editor::ProjectTemplate::Basic3D);
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
    // Only meshes, models and textures make entities; textures make sprites.
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
    REQUIRE(Spot::MaterialSource{.specular = 0.25f}.SaveToFile(file));
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
    const ImVec2 field = fixture.Center(assets.GetItemRect("/specular"));
    fixture.imgui.Drag(field, {field.x + 60.0f, field.y});
    const auto saved = Spot::MaterialSource::LoadFromFile(file);
    REQUIRE(saved.has_value());
    CHECK(saved->specular > 0.25f);
    REQUIRE(assets.GetHistory());
    CHECK(assets.GetHistory()->GetUndoCount() == 1);

    assets.GetHistory()->Undo();
    CHECK(Spot::MaterialSource::LoadFromFile(file)->specular == 0.25f);
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
