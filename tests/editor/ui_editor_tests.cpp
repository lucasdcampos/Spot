#include "editor/headless_imgui.h"
#include "scene/entity_actions.h"
#include "scene/scene_document.h"
#include "viewport/ui_rect_gizmo.h"
#include <spot/scene/ui/ui_system.h>

#include <catch2/catch_test_macros.hpp>
#include <spot.h>

TEST_CASE("UI creation authors a canvas and widget as one undoable selection", "[editor][ui]") {
    Spot::Editor::SceneDocument document;
    REQUIRE(Spot::Editor::CreateEntity(document, Spot::Editor::EntityTemplate::UiButton));
    auto* button = document.GetScene().GetEntity(document.GetSelection().GetPrimary());
    REQUIRE(button);
    REQUIRE(button->GetComponent<Spot::UiRect>());
    REQUIRE(button->GetComponent<Spot::UiButton>());
    REQUIRE(button->GetParent()->GetComponent<Spot::Canvas>());
    const auto id = button->GetId();
    CHECK(document.GetHistory().GetUndoCount() == 1);
    document.Undo();
    CHECK(document.GetScene().GetEntityCount() == 0);
    document.Redo();
    CHECK(document.GetSelection().GetPrimary() == id);
    REQUIRE(document.GetScene().GetEntity(id));
    CHECK(document.GetScene().GetEntity(id)->FindChild("Label")->GetComponent<Spot::UiText>());
}

TEST_CASE("Rect gizmos move resize cancel and undo without changing Transform", "[editor][ui]") {
    Spot::Editor::SceneDocument document;
    auto& canvas = document.GetScene().CreateEntity("Canvas");
    canvas.AddComponent<Spot::Canvas>().settings.scale = 2;
    auto& entity = canvas.CreateChild("Rect");
    auto& rect = entity.AddComponent<Spot::UiRect>();
    Spot::Editor::UiRectGizmo gizmo;
    REQUIRE(gizmo.Begin(document, entity.GetId(), {800, 600}));
    CHECK(gizmo.Translate(document, {20, 10}));
    CHECK(rect.layout.offset == Spot::Vector2{10, 5});
    REQUIRE(gizmo.Finish(document));
    document.Undo();
    CHECK(rect.layout.offset == Spot::Vector2{});
    document.Redo();
    CHECK(rect.layout.offset == Spot::Vector2{10, 5});
    REQUIRE(gizmo.Begin(document, entity.GetId(), {800, 600}));
    CHECK(gizmo.Resize(document, {20, 10}, {1, 1}));
    CHECK(rect.layout.sizeDelta == Spot::Vector2{110, 35});
    CHECK(rect.layout.offset == Spot::Vector2{15, 7.5f});
    CHECK(entity.GetTransform().GetPosition() == Spot::Vector3{});
    gizmo.Cancel(document);
    CHECK(rect.layout.sizeDelta == Spot::Vector2{100, 30});
    CHECK(document.GetHistory().GetUndoCount() == 1);
}

TEST_CASE("Anchor presets preserve rectangles and undo their serialized fields", "[editor][ui]") {
    Spot::Editor::SceneDocument document;
    auto& root = document.GetScene().CreateEntity("Canvas");
    root.AddComponent<Spot::Canvas>();
    auto& entity = root.CreateChild("Rect");
    auto& rect = entity.AddComponent<Spot::UiRect>();
    auto& ui = document.GetScene().GetOrAddSystem<Spot::UiSystem>();
    ui.Layout({800, 600});
    const auto bounds = ui.Find(entity.GetId())->bounds;
    REQUIRE(Spot::Editor::SetUiAnchorPreset(document, entity.GetId(), {800, 600}, {}, {1, 1}, {0.5f, 0.5f}));
    ui.Layout({800, 600});
    CHECK(ui.Find(entity.GetId())->bounds == bounds);
    CHECK(rect.layout.sizeDelta == Spot::Vector2{-700, -570});
    document.Undo();
    CHECK(rect.layout.anchorMin == Spot::Vector2{0.5f, 0.5f});
    document.Redo();
    CHECK(rect.layout.anchorMin == Spot::Vector2{});
    REQUIRE(Spot::Editor::SetUiAnchorPreset(document, entity.GetId(), {800, 600}, {0, 1}, {0, 1}, {0, 1}, false));
    CHECK(rect.layout.offset == Spot::Vector2{});
    CHECK(rect.layout.sizeDelta == Spot::Vector2{100, 30});
}

TEST_CASE("Rect edits reject stale component identities and group-controlled positions", "[editor][ui]") {
    Spot::Editor::SceneDocument document;
    auto& root = document.GetScene().CreateEntity("Canvas");
    root.AddComponent<Spot::Canvas>();
    auto& entity = root.CreateChild("Rect");
    entity.AddComponent<Spot::UiRect>();
    Spot::Editor::UiRectGizmo gizmo;
    REQUIRE(gizmo.Begin(document, entity.GetId(), {800, 600}));
    entity.RemoveComponent<Spot::UiRect>();
    entity.AddComponent<Spot::UiRect>();
    CHECK_FALSE(gizmo.Translate(document, {100, 100}));
    CHECK_FALSE(gizmo.Finish(document));
    root.AddComponent<Spot::UiLayoutGroup>();
    CHECK_FALSE(gizmo.Begin(document, entity.GetId(), {800, 600}));
}

TEST_CASE("Rect handles respond to actual ImGui pointer drags", "[editor][ui]") {
    Spot::Tests::HeadlessImGui imgui({800, 600});
    Spot::Editor::SceneDocument document;
    auto& canvas = document.GetScene().CreateEntity("Canvas");
    canvas.AddComponent<Spot::Canvas>();
    auto& entity = canvas.CreateChild("Rect");
    auto& rect = entity.AddComponent<Spot::UiRect>();
    document.GetSelection().Select(entity.GetId());
    Spot::Editor::UiRectGizmo gizmo;
    ImVec2 origin;
    imgui.SetDraw([&] {
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({800, 600});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::Begin("UI Handles", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        origin = ImGui::GetCursorScreenPos();
        gizmo.Draw(document, origin, {800, 600}, {800, 600});
        ImGui::End();
        ImGui::PopStyleVar();
    });
    imgui.Frame();
    imgui.Frame();
    imgui.Drag({origin.x + 400, origin.y + 300}, {origin.x + 420, origin.y + 310});
    CHECK(rect.layout.offset == Spot::Vector2{20, -10});
    CHECK(document.GetHistory().GetUndoCount() == 1);
    imgui.Drag({origin.x + 470, origin.y + 295}, {origin.x + 490, origin.y + 285});
    CHECK(rect.layout.sizeDelta == Spot::Vector2{120, 40});
    CHECK(rect.layout.offset == Spot::Vector2{30, -5});
    CHECK(document.GetHistory().GetUndoCount() == 2);
    CHECK_FALSE(gizmo.IsDragging());
}
