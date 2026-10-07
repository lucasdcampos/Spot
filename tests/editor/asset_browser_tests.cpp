#include "headless_imgui.h"
#include "temp_directory.h"

#include "app/editor_context.h"
#include "assets/asset_browser_model.h"
#include "panels/asset_browser_panel.h"
#include "project/editor_project.h"
#include "project/project_templates.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
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
