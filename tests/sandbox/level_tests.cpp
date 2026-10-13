#include "day_cycle.h"
#include "player_controller.h"
#include "playground_fixture.h"

#include <catch2/catch_approx.hpp>

TEST_CASE("The authored workshop has a walkable entrance, solid windows and an accessible roof", "[sandbox][level]") {
    SandboxTests::Playground game;
    auto& physics = game.scene.GetPhysics();
    auto* workshop = game.scene.FindEntity("Workshop");
    REQUIRE(workshop);
    CHECK(game.scene.FindEntitiesWithTag("SpawnArea").size() == 3);
    for (const auto* child : workshop->GetChildren()) {
        CHECK(child->GetComponent<Spot::MeshRenderer>());
        if (!child->GetComponent<Spot::Light>()) {
            CHECK(child->GetComponent<Spot::BoxCollider>());
        }
    }
    const auto entrance = physics.Raycast({10.5f, 1.5f, -4}, {0, 0, -1}, 15);
    REQUIRE(entrance);
    CHECK(game.scene.GetEntity(entrance->entity)->GetName() == "Workshop North Wall");
    const auto window = physics.Raycast({14, 1.5f, -12}, {1, 0, 0}, 4);
    REQUIRE(window);
    CHECK(game.scene.GetEntity(window->entity)->GetName() == "Workshop Window");
    const auto roof = physics.Raycast({10, 6, -12}, {0, -1, 0}, 4);
    REQUIRE(roof);
    CHECK(game.scene.GetEntity(roof->entity)->GetName() == "Workshop Roof");

    auto* player = game.scene.FindEntity("Player");
    REQUIRE(player);
    player->GetTransform().SetPosition({10.5f, 1.1f, -4});
    game.Run(30);
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::W, false});
    game.Run(72);
    game.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::W});
    CHECK(player->GetTransform().GetWorldPosition().z < -9);
    CHECK(player->GetTransform().GetWorldPosition().y == Catch::Approx(1).margin(.2f));

    for (int i = 0; i < 14; ++i) {
        const auto step = physics.Raycast({18, 5, 1.f - static_cast<float>(i) * .5f}, {0, -1, 0}, 6);
        REQUIRE(step);
        CHECK(step->point.y == Catch::Approx(static_cast<float>(i + 1) * .25f).margin(.01f));
    }
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
    REQUIRE(lamps.size() == 2);
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
