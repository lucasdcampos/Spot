#include "editor/headless_imgui.h"
#include "inspector/tilemap_painter.h"
#include "scene/entity_actions.h"
#include <catch2/catch_test_macros.hpp>
#include <spot.h>

using namespace Spot;
using namespace Spot::Editor;
TEST_CASE("Tilemap creation and brush strokes undo redo and cancel by component identity", "[editor][tilemap]") {
    SceneDocument document;
    REQUIRE(CreateEntity(document, EntityTemplate::Tilemap));
    const auto id = document.GetSelection().GetPrimary();
    auto* entity = document.GetScene().GetEntity(id);
    REQUIRE(entity);
    CHECK(entity->HasComponent<TilemapCollider2D>());
    TilemapPainter painter;
    REQUIRE(painter.Begin(document, id, 0));
    REQUIRE(painter.Paint(document, 0, 0));
    REQUIRE(painter.Paint(document, 5, 5));
    REQUIRE(painter.Finish(document));
    CHECK(document.GetHistory().GetUndoCount() == 2);
    auto* map = entity->GetComponent<Tilemap>();
    for (int i = 0; i <= 5; ++i)
        CHECK(map->GetCell(i, i) == 0);
    document.Undo();
    for (int i = 0; i <= 5; ++i)
        CHECK(map->GetCell(i, i) == -1);
    document.Redo();
    CHECK(map->GetCell(4, 4) == 0);
    REQUIRE(painter.Begin(document, id, -1));
    painter.Paint(document, 4, 4);
    painter.Cancel(document);
    CHECK(map->GetCell(4, 4) == 0);
    CHECK(document.GetHistory().GetUndoCount() == 2);
    REQUIRE(painter.Begin(document, id, 0));
    entity->RemoveComponent<Tilemap>();
    entity->AddComponent<Tilemap>();
    CHECK_FALSE(painter.Paint(document, 1, 1));
    CHECK_FALSE(painter.Finish(document));
    CHECK(entity->GetComponent<Tilemap>()->GetCell(1, 1) == -1);
}
TEST_CASE("The tilemap painter handles actual ImGui drags and erasure", "[editor][tilemap][ui]") {
    SceneDocument document;
    auto& entity = document.GetScene().CreateEntity();
    auto& map = entity.AddComponent<Tilemap>();
    map.Resize(6, 4);
    TilemapPainter painter;
    Tests::HeadlessImGui imgui;
    imgui.SetDraw([&] {
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({700, 600});
        ImGui::Begin("Paint");
        painter.Draw(document, entity.GetId());
        ImGui::End();
    });
    imgui.Frame();
    imgui.Frame();
    REQUIRE(painter.GetGridRect());
    const auto rect = *painter.GetGridRect();
    const ImVec2 first{rect.first.x + 12, rect.first.y + 84}, last{rect.first.x + 132, rect.first.y + 12};
    imgui.Drag(first, last, 1);
    CHECK(map.GetCell(0, 0) == 0);
    CHECK(map.GetCell(5, 3) == 0);
    CHECK(document.GetHistory().GetUndoCount() == 1);
    imgui.Click(last, 0, ImGuiMouseButton_Right);
    CHECK(map.GetCell(5, 3) == -1);
    CHECK(document.GetHistory().GetUndoCount() == 2);
    document.Undo();
    CHECK(map.GetCell(5, 3) == 0);
}
