#include "headless_imgui.h"

#include "app/editor_context.h"
#include "panels/hierarchy_panel.h"
#include "scene/entity_actions.h"
#include "scene/scene_workspace.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using Spot::Entity;
using Spot::EntityId;

namespace {

// A new scene, with a sun and a sky, open in a workspace whose hierarchy draws headless.
struct HierarchyFixture {
    std::unique_ptr<Spot::Window> window = Spot::Window::Create();
    Spot::Tests::HeadlessImGui imgui;
    Spot::Editor::EditorSettings settings;
    std::unique_ptr<Spot::Editor::EditorContext> context;
    std::unique_ptr<Spot::Editor::SceneWorkspace> workspace;
    std::unique_ptr<Spot::Editor::HierarchyPanel> panel;
    Spot::Editor::SceneDocument* document = nullptr;

    HierarchyFixture() {
        REQUIRE(window != nullptr);
        context = std::make_unique<Spot::Editor::EditorContext>(*window, nullptr, settings);
        workspace = std::make_unique<Spot::Editor::SceneWorkspace>(*context);
        context->SetWorkspace(workspace.get());
        panel = std::make_unique<Spot::Editor::HierarchyPanel>(*context);
        document = &workspace->NewScene();
        REQUIRE(Spot::Editor::CreateEntity(*document, Spot::Editor::EntityTemplate::Cube));
        document->GetSelection().Clear();
        Frame();
    }

    ~HierarchyFixture() { context->SetWorkspace(nullptr); }

    void Frame() {
        imgui.BeginFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({400.0f, 500.0f});
        panel->Draw();
        imgui.EndFrame();
    }

    Entity& Find(const char* name) {
        Entity* entity = document->GetScene().FindEntity(name);
        REQUIRE(entity);
        return *entity;
    }

    ImVec2 RowPoint(const char* name, float height = 0.5f) {
        const auto rect = panel->GetRowRect(Find(name).GetId());
        REQUIRE(rect.has_value());
        return {rect->first.x + 60.0f, rect->first.y + (rect->second.y - rect->first.y) * height};
    }

    void Click(ImVec2 point, ImGuiKeyChord modifiers = 0) {
        ImGuiIO& io = ImGui::GetIO();
        const auto setModifiers = [&](bool down) {
            if (modifiers & ImGuiMod_Ctrl) {
                io.AddKeyEvent(ImGuiMod_Ctrl, down);
            }
            if (modifiers & ImGuiMod_Shift) {
                io.AddKeyEvent(ImGuiMod_Shift, down);
            }
        };
        setModifiers(true);
        io.AddMousePosEvent(point.x, point.y);
        Frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        Frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        Frame();
        setModifiers(false);
        Frame();
    }

    void Press(ImGuiKeyChord chord) {
        ImGuiIO& io = ImGui::GetIO();
        const ImGuiKey key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
        if (chord & ImGuiMod_Ctrl) {
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
        }
        io.AddKeyEvent(key, true);
        Frame();
        io.AddKeyEvent(key, false);
        if (chord & ImGuiMod_Ctrl) {
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
        }
        Frame();
    }

    void Drag(ImVec2 from, ImVec2 to) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(from.x, from.y);
        Frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        Frame();
        for (int step = 1; step <= 6; ++step) {
            const float t = static_cast<float>(step) / 6.0f;
            io.AddMousePosEvent(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
            Frame();
        }
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        Frame();
        Frame();
    }

    std::vector<std::string> RootNames() {
        std::vector<std::string> names;
        for (const Entity* root : document->GetScene().GetRootEntities()) {
            names.push_back(root->GetName());
        }
        return names;
    }
};

} // namespace

TEST_CASE("Clicking rows selects entities, with Ctrl and Shift adding to it", "[editor][hierarchy][ui]") {
    HierarchyFixture fixture;
    const EntityId sun = fixture.Find("Sun").GetId();
    const EntityId sky = fixture.Find("Sky").GetId();
    const EntityId cube = fixture.Find("Cube").GetId();

    fixture.Click(fixture.RowPoint("Sun"));
    CHECK(fixture.document->GetSelection().GetPrimary() == sun);
    CHECK(fixture.document->GetSelection().GetEntities().size() == 1);

    fixture.Click(fixture.RowPoint("Cube"), ImGuiMod_Ctrl);
    CHECK(fixture.document->GetSelection().Contains(sun));
    CHECK(fixture.document->GetSelection().Contains(cube));
    CHECK_FALSE(fixture.document->GetSelection().Contains(sky));

    fixture.Click(fixture.RowPoint("Sun"));
    fixture.Click(fixture.RowPoint("Cube"), ImGuiMod_Shift);
    CHECK(fixture.document->GetSelection().GetEntities().size() == 3);
}

TEST_CASE("Keys in the hierarchy rename, duplicate and delete, all undoable", "[editor][hierarchy][ui]") {
    HierarchyFixture fixture;
    fixture.Click(fixture.RowPoint("Cube"));

    fixture.Press(ImGuiKey_F2);
    fixture.Frame();
    ImGui::GetIO().AddInputCharactersUTF8("Crate");
    fixture.Frame();
    fixture.Press(ImGuiKey_Enter);
    CHECK(fixture.document->GetScene().FindEntity("Crate"));

    fixture.Press(ImGuiMod_Ctrl | ImGuiKey_D);
    CHECK(fixture.RootNames() == std::vector<std::string>{"Sun", "Sky", "Crate", "Crate"});
    const EntityId copy = fixture.document->GetSelection().GetPrimary();
    REQUIRE(fixture.document->GetScene().FindEntity("Crate"));
    CHECK(copy != fixture.document->GetScene().FindEntity("Crate")->GetId());

    fixture.Press(ImGuiKey_Delete);
    CHECK_FALSE(fixture.document->GetScene().GetEntity(copy));
    CHECK(fixture.document->GetSelection().IsEmpty());

    fixture.document->Undo();
    CHECK(fixture.document->GetScene().GetEntity(copy));
    fixture.document->Undo();
    fixture.document->Undo();
    CHECK(fixture.RootNames() == std::vector<std::string>{"Sun", "Sky", "Cube"});
}

TEST_CASE("Dragging rows reparents and reorders entities", "[editor][hierarchy][ui]") {
    HierarchyFixture fixture;

    fixture.Drag(fixture.RowPoint("Cube"), fixture.RowPoint("Sun"));
    CHECK(fixture.Find("Cube").GetParent() == &fixture.Find("Sun"));

    fixture.Frame();
    fixture.Drag(fixture.RowPoint("Sky"), fixture.RowPoint("Sun", 0.05f));
    CHECK(fixture.RootNames() == std::vector<std::string>{"Sky", "Sun"});

    fixture.document->Undo();
    fixture.document->Undo();
    CHECK(fixture.RootNames() == std::vector<std::string>{"Sun", "Sky", "Cube"});
}

TEST_CASE("Copied entities paste into another scene", "[editor][hierarchy]") {
    HierarchyFixture fixture;
    fixture.document->GetSelection().Select(fixture.Find("Cube").GetId());
    REQUIRE(Spot::Editor::CopySelection(*fixture.document));

    Spot::Editor::SceneDocument& other = fixture.workspace->NewScene();
    REQUIRE(Spot::Editor::PasteClipboard(other));

    REQUIRE(other.GetScene().FindEntity("Cube"));
    CHECK(other.GetSelection().GetPrimary() == other.GetScene().FindEntity("Cube")->GetId());
    CHECK(other.GetScene().FindEntity("Cube")->GetComponent<Spot::MeshRenderer>());
}
