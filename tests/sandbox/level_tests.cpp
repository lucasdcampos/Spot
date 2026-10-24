#include "day_cycle.h"
#include "player_controller.h"
#include "playground_fixture.h"
#include "spot/scene/physics/physics_world.h"

#include <catch2/catch_approx.hpp>

TEST_CASE("The workshop has a wide entrance, glass windows and steel stairs up to its roof", "[sandbox][level]") {
    SandboxTests::Playground game;
    auto& physics = game.scene.GetOrAddSystem<Spot::PhysicsWorld>();
    auto* workshop = game.scene.FindEntity("Workshop");
    REQUIRE(workshop);
    CHECK(game.scene.FindEntitiesWithTag("SpawnArea").size() == 3);
    // Everything solid in it collides; lamps and signs only shine.
    for (const auto* child : workshop->GetChildren()) {
        if (child->GetComponent<Spot::MeshRenderer>() && !child->GetComponent<Spot::Light>()) {
            CAPTURE(child->GetName());
            CHECK(child->GetComponent<Spot::BoxCollider>());
        }
    }
    const auto entrance = physics.Raycast({19, 1.5f, -20}, {0, 0, -1}, 40);
    REQUIRE(entrance);
    CHECK(game.scene.GetEntity(entrance->entity)->GetName() == "Workshop North Wall");
    const auto window = physics.Raycast({26, 2.2f, -36.5f}, {1, 0, 0}, 6);
    REQUIRE(window);
    CHECK(game.scene.GetEntity(window->entity)->GetName() == "Workshop Window");
    const auto roof = physics.Raycast({19, 9, -37}, {0, -1, 0}, 4);
    REQUIRE(roof);
    CHECK(game.scene.GetEntity(roof->entity)->GetName() == "Workshop Roof");

    // In through the door, onto the workshop's floor.
    auto* player = game.scene.FindEntity("Player");
    REQUIRE(player);
    player->GetTransform().SetPosition({19, 1.1f, -20});
    game.Run(30);
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::W, false});
    game.Run(72);
    game.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::W});
    CHECK(player->GetTransform().GetWorldPosition().z < -25);
    CHECK(player->GetTransform().GetWorldPosition().y == Catch::Approx(0.95f).margin(.2f));

    // Twenty-four even steps up the outside of the west wall, then a landing level with the roof.
    const float rise = 6.3f / 25.0f;
    for (int i = 0; i < 24; ++i) {
        const auto step = physics.Raycast({6.8f, 8, -28.6f - 0.45f * static_cast<float>(i) - 0.225f}, {0, -1, 0}, 9);
        REQUIRE(step);
        CHECK(step->point.y == Catch::Approx(rise * static_cast<float>(i + 1)).margin(.01f));
    }
    player->GetTransform().SetPosition({6.8f, 1.1f, -27.5f});
    game.Run(30);
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::W, false});
    game.Run(160);
    game.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::W});
    CHECK(player->GetTransform().GetWorldPosition().y > 7.0f);
}

TEST_CASE("Workshop lamps keep the interior lit throughout the day and pause freezes the cycle",
          "[sandbox][level][ui]") {
    SandboxTests::Playground game;
    auto* day = game.scene.FindComponent<DayCycle>();
    REQUIRE(day);
    day->timeOfDay = .99f;
    day->dayLength = 1;
    game.Run(2);
    CHECK(day->timeOfDay < .04f);
    std::vector<Spot::PointLight*> lamps;
    game.scene.ForEachComponent<Spot::PointLight>([&](Spot::PointLight& lamp) {
        if (lamp.GetEntity().GetParent() == game.scene.FindEntity("Workshop")) {
            lamps.push_back(&lamp);
        }
    });
    REQUIRE(lamps.size() == 4);
    for (const auto* lamp : lamps) {
        CHECK(lamp->IsActiveAndEnabled());
        CHECK(lamp->GetIntensity() > 0);
        CHECK(lamp->IsCastingShadows());
    }
    auto* ui = game.scene.FindComponent<RuntimeInterface>();
    const float pausedTime = day->timeOfDay;
    ui->OpenPauseMenu();
    game.Run(30);
    CHECK(day->timeOfDay == pausedTime);
    ui->CloseMenus();
    game.Run(2);
    CHECK(day->timeOfDay > pausedTime);
}

TEST_CASE("Street lamps light up at night and go out by day", "[sandbox][level]") {
    SandboxTests::Playground game;
    auto* day = game.scene.FindComponent<DayCycle>();
    REQUIRE(day);
    std::vector<Spot::PointLight*> lamps;
    game.scene.ForEachEntity([&](Spot::Entity& entity) {
        if (entity.GetName() == "Street Light") {
            lamps.push_back(entity.GetComponent<Spot::PointLight>());
        }
    });
    REQUIRE(lamps.size() >= 8);
    day->timeOfDay = 0.5f;
    game.Run(2);
    for (const auto* lamp : lamps) {
        CHECK_FALSE(lamp->IsEnabled());
    }
    day->timeOfDay = 0.0f;
    game.Run(2);
    for (const auto* lamp : lamps) {
        CHECK(lamp->IsEnabled());
        CHECK(lamp->GetIntensity() > 10.0f);
    }
}
