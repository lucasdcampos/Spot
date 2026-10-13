#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <spot.h>

using namespace Spot;
TEST_CASE("Tilemap grids resize preserve cells and reject invalid edits", "[tilemap]") {
    TilemapData data;
    REQUIRE(data.Resize(3, 2));
    REQUIRE(data.SetCell(2, 1, 0));
    CHECK_FALSE(data.SetCell(2, 1, 0));
    CHECK_FALSE(data.SetCell(-1, 0, 0));
    CHECK_FALSE(data.SetCell(0, 0, 4));
    REQUIRE(data.Resize(4, 3));
    CHECK(data.GetCell(2, 1) == 0);
    CHECK(data.GetCell(3, 2) == -1);
    CHECK_FALSE(data.Resize(257, 1));
    CHECK_FALSE(data.Resize(1, 0));
    REQUIRE(data.IsValid());
    data.cells.push_back(0);
    CHECK_FALSE(data.IsValid());
}
TEST_CASE("Tilemap collision rectangles merge row runs without filling holes", "[tilemap][physics2d]") {
    TilemapData data;
    data.Resize(4, 3);
    data.definitions = {{0, true}, {1, false}};
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            data.SetCell(x, y, 0);
    data.SetCell(3, 2, 0);
    data.SetCell(0, 2, 1);
    data.cellSize = {2, 3};
    std::vector<TileCollisionBox> boxes;
    data.BuildCollisionBoxes(boxes);
    REQUIRE(boxes.size() == 2);
    CHECK(boxes[0].center == Vector2{3, 3});
    CHECK(boxes[0].size == Vector2{6, 6});
    CHECK(boxes[1].center == Vector2{7, 7.5f});
    CHECK(boxes[1].size == Vector2{2, 3});
    const auto capacity = boxes.capacity();
    data.BuildCollisionBoxes(boxes);
    CHECK(boxes.capacity() == capacity);
    data.Resize(256, 256);
    data.cellSize = {1, 1};
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
            data.SetCell(x, y, (x + y) % 2 == 0 ? 0 : -1);
    data.BuildCollisionBoxes(boxes);
    CHECK(boxes.size() == 32768);
    for (const auto& box : boxes)
        CHECK(box.size == Vector2{1, 1});
}
TEST_CASE("Tilemap colliders rebuild after painting and follow transforms and activity", "[tilemap][physics2d]") {
    Scene scene;
    auto& entity = scene.CreateEntity("Map");
    auto& map = entity.AddComponent<Tilemap>();
    auto& collider = entity.AddComponent<TilemapCollider2D>();
    auto data = map.GetData();
    data.Resize(4, 2);
    data.definitions[0].solid = true;
    data.SetCell(0, 0, 0);
    REQUIRE(map.SetData(data));
    entity.GetTransform().SetPosition({2, 3, 0});
    entity.GetTransform().SetScale({2, 2, 1});
    auto& world = scene.GetPhysics2D();
    auto hit = world.Raycast({0, 4}, {1, 0}, 10);
    REQUIRE(hit);
    CHECK(hit->collider == &collider);
    CHECK(hit->distance == Catch::Approx(2));
    map.SetCell(0, 0, -1);
    CHECK_FALSE(world.Raycast({0, 4}, {1, 0}, 10));
    map.SetCell(1, 0, 0);
    CHECK(world.Raycast({0, 4}, {1, 0}, 10)->distance == Catch::Approx(4));
    map.SetEnabled(false);
    CHECK_FALSE(world.Raycast({0, 4}, {1, 0}, 10));
    map.SetEnabled(true);
    CHECK(world.Raycast({0, 4}, {1, 0}, 10));
    Scene restored;
    REQUIRE(restored.LoadFromString(scene.SaveToString()));
    auto* restoredMap = restored.FindEntity("Map");
    REQUIRE(restoredMap);
    CHECK(restoredMap->GetComponent<Tilemap>()->GetCell(1, 0) == 0);
    CHECK(restored.GetPhysics2D().Raycast({0, 4}, {1, 0}, 10));
}
TEST_CASE("A falling 2D body rests on tiles and falls through erased tiles", "[tilemap][physics2d]") {
    Scene scene;
    auto& floor = scene.CreateEntity();
    auto& map = floor.AddComponent<Tilemap>();
    auto data = map.GetData();
    data.Resize(4, 1);
    data.definitions[0].solid = true;
    for (int x = 0; x < 4; ++x)
        data.SetCell(x, 0, 0);
    REQUIRE(map.SetData(data));
    floor.AddComponent<TilemapCollider2D>();
    auto& ball = scene.CreateEntity();
    ball.GetTransform().SetPosition({1.5f, 3, 5});
    ball.AddComponent<CircleCollider2D>();
    ball.AddComponent<Rigidbody2D>();
    for (int i = 0; i < 180; ++i)
        scene.Update(Scene::DefaultFixedTimeStep);
    CHECK(ball.GetTransform().GetWorldPosition().y == Catch::Approx(1.5f).margin(0.04f));
    REQUIRE(map.SetCell(1, 0, -1));
    for (int i = 0; i < 120; ++i)
        scene.Update(Scene::DefaultFixedTimeStep);
    CHECK(ball.GetTransform().GetWorldPosition().y < 0);
    CHECK(ball.GetTransform().GetWorldPosition().z == 5);
}
