// The sandbox's game code, run headless on its real scenes: the engine's integration test, which shows a regression
// as something broken in the game.
#include "log_capture.h"
#include "player_controller.h"

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

using Catch::Approx;
using Spot::Entity;
using Spot::Key;
using Spot::Scene;

namespace {

constexpr float Step = Scene::DefaultFixedTimeStep;
const std::filesystem::path Assets = SANDBOX_ASSETS_DIR;

// The playground, loaded from its source with the sandbox's components and the assets the build cooks for it, and
// input the test presses keys on. There is no GPU, so meshes and fonts do not load, which the game's logic and
// physics do not need; the log, which says so, is kept from the output.
struct Playground {
    std::shared_ptr<Spot::Tests::CaptureSink> log = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks{{log}};
    Spot::InputState input;
    Spot::InputState* previousInput = Spot::Input::GetState();
    Spot::AssetManager assets{SANDBOX_IMPORTED_DIR};
    Scene scene;
    Entity* player = nullptr;

    Playground() {
        Spot::Detail::GameCode::Load();
        Spot::Input::SetState(&input);
        REQUIRE(scene.LoadFromFile(Assets / "scenes" / "playground.scene", &assets));
        player = scene.FindEntity("Player");
        REQUIRE(player != nullptr);
    }

    ~Playground() {
        Spot::Input::SetState(previousInput);
        // Components that threw were logged; nothing else in the game should have.
        for (const auto& record : log->GetRecords()) {
            CHECK(record.message.find(" threw in ") == std::string::npos);
        }
    }

    void Run(float seconds) {
        const auto frames = std::lround(seconds / Step);
        for (long i = 0; i < frames; ++i) {
            scene.Update(Step);
            input.BeginFrame();
        }
    }

    void Press(Key key) { input.OnEvent(Spot::KeyPressedEvent{.key = key, .repeat = false}); }
    void Release(Key key) { input.OnEvent(Spot::KeyReleasedEvent{.key = key}); }

    Spot::Vector3 PlayerPosition() const { return player->GetTransform().GetPosition(); }
    Spot::CharacterController& Controller() const { return *player->GetComponent<Spot::CharacterController>(); }
};

} // namespace

TEST_CASE("The playground runs without faults", "[sandbox]") {
    Playground playground;
    playground.Run(5.0f);
    playground.scene.ForEachEntity([](Entity& entity) {
        for (const Spot::Component* component : entity.GetComponents<Spot::Component>()) {
            CAPTURE(entity.GetName());
            CHECK_FALSE(component->IsFaulted());
        }
    });
}

TEST_CASE("The player lands, walks where it looks and jumps", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    REQUIRE(playground.Controller().IsGrounded());
    const Spot::Vector3 start = playground.PlayerPosition();
    // The capsule, 1.8 tall, stands on the ground's top at y = 0.
    CHECK(start.y == Approx(0.9f).margin(0.05f));

    playground.Press(Key::W);
    playground.Run(1.0f);
    playground.Release(Key::W);
    const Spot::Vector3 walked = playground.PlayerPosition();
    // It looks down -Z, and walks at 5 meters per second.
    CHECK(start.z - walked.z == Approx(5.0f).margin(0.5f));
    CHECK(std::abs(walked.x - start.x) < 0.1f);

    playground.Press(Key::Space);
    playground.Run(0.3f);
    CHECK(playground.PlayerPosition().y > start.y + 0.5f);
    CHECK_FALSE(playground.Controller().IsGrounded());
    playground.Run(1.5f);
    CHECK(playground.Controller().IsGrounded());
    CHECK(playground.PlayerPosition().y == Approx(start.y).margin(0.05f));
}

TEST_CASE("The player crouches, slower and lower, and stands only with room above", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    const Spot::Vector3 start = playground.PlayerPosition();

    playground.Press(Key::LeftControl);
    playground.Press(Key::W);
    playground.Run(1.0f);
    playground.Release(Key::W);
    CHECK(playground.Controller().GetHeight() == Approx(1.0f));
    // Crouching walks at 2.5 meters per second, with the feet where they were.
    CHECK(start.z - playground.PlayerPosition().z == Approx(2.5f).margin(0.3f));
    CHECK(playground.PlayerPosition().y == Approx(start.y).margin(0.05f));

    // A ceiling just above the crouching player keeps it down.
    auto& ceiling = playground.scene.CreateEntity("Ceiling");
    ceiling.GetTransform().SetPosition(playground.PlayerPosition() + Spot::Vector3{0.0f, 0.75f, 0.0f});
    ceiling.AddComponent<Spot::BoxCollider>(Spot::Vector3{3.0f, 0.2f, 3.0f});
    playground.Run(0.1f);
    playground.Release(Key::LeftControl);
    playground.Run(0.5f);
    CHECK(playground.Controller().GetHeight() == Approx(1.0f));

    ceiling.Destroy();
    playground.Run(0.5f);
    CHECK(playground.Controller().GetHeight() == Approx(1.8f));
}

TEST_CASE("Noclip flies the player through everything, and walking lands it again", "[sandbox]") {
    Playground playground;
    playground.Run(1.5f);
    const float ground = playground.PlayerPosition().y;

    playground.Press(Key::V);
    playground.Run(0.1f);
    playground.Release(Key::V);
    playground.Press(Key::Space);
    playground.Run(1.0f);
    playground.Release(Key::Space);
    // Ten meters per second, straight up, with nothing pulling it down.
    CHECK(playground.PlayerPosition().y == Approx(ground + 10.0f).margin(1.0f));
    playground.Run(1.0f);
    CHECK(playground.PlayerPosition().y == Approx(ground + 10.0f).margin(1.0f));

    playground.Press(Key::V);
    playground.Run(3.0f);
    CHECK(playground.Controller().IsGrounded());
    CHECK(playground.PlayerPosition().y == Approx(ground).margin(0.05f));
}

TEST_CASE("The player climbs the stairs and the ramps of the level's meshes", "[sandbox]") {
    Playground playground;
    // At the foot of the stairs, which rise toward +X onto the platform, whose top is at y = 2.
    playground.player->GetTransform().SetPosition({-17.0f, 0.95f, -8.0f});
    playground.Run(0.5f);
    playground.Press(Key::D);
    playground.Run(1.5f);
    playground.Release(Key::D);
    CHECK(playground.PlayerPosition().x > -12.5f);
    CHECK(playground.PlayerPosition().y == Approx(2.9f).margin(0.1f));

    // At the foot of a ramp, which rises toward -Z to a height of 1 over three meters.
    playground.player->GetTransform().SetPosition({-3.0f, 0.95f, 5.0f});
    playground.Run(0.5f);
    playground.Press(Key::W);
    playground.Run(0.7f);
    CHECK(playground.PlayerPosition().y > 1.3f);
    CHECK(playground.Controller().IsGrounded());
}
