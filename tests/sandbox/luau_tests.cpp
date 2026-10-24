
#include <spot/scene/scripting/script_component.h>

#include "day_cycle.h"
#include "playground_fixture.h"

#include <catch2/catch_approx.hpp>

#include <array>

TEST_CASE("The sandbox Luau scripts animate the real monument and toggle its neon", "[sandbox][luau]") {
    SandboxTests::Playground game;
    auto* sign = game.scene.FindEntity("Monument Sign");
    auto* plinth = game.scene.FindEntity("Monument Plinth");
    REQUIRE(sign);
    REQUIRE(plinth);
    auto* script = sign->GetComponent<Spot::ScriptComponent>();
    REQUIRE(script);
    REQUIRE(script->script);
    CHECK(script->script->sourceName == "scripts/monument_sign.luau");
    auto* neon = sign->FindChild("Neon");
    REQUIRE(neon);
    auto* mesh = neon->GetComponent<Spot::MeshRenderer>();
    REQUIRE(mesh);
    const auto plinthPosition = plinth->GetTransform().GetPosition();
    const float initialHeight = sign->GetTransform().GetPosition().y;
    game.Run(60);
    CHECK(sign->GetTransform().GetPosition().y > initialHeight + 0.02f);
    CHECK(sign->GetTransform().GetScale().x > 1.01f);
    game.Run(60);
    const float peak = sign->GetTransform().GetPosition().y;
    game.Run(60);
    CHECK(sign->GetTransform().GetPosition().y < peak - 0.02f);
    CHECK(plinth->GetTransform().GetPosition() == plinthPosition);
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::N, false});
    game.Step();
    CHECK_FALSE(mesh->IsEnabled());
    game.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::N});
    game.Step();
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::N, false});
    game.Step();
    CHECK(mesh->IsEnabled());
    CHECK_FALSE(script->IsFaulted());
}

TEST_CASE("The sandbox Luau music script fades the playing soundtrack and respects input capture", "[sandbox][luau]") {
    SandboxTests::Playground game(true);
    REQUIRE(game.audio);
    // A manual device has no audio thread to consume commands or finish one-shot voices.
    const auto run = [&](int frames) {
        std::array<float, 1600> samples{};
        for (int frame = 0; frame < frames; ++frame) {
            game.Step();
            game.audio->Render(samples);
        }
    };
    auto* music = game.scene.FindEntity("Music");
    REQUIRE(music);
    auto* audio = music->GetComponent<Spot::AudioSource>();
    auto* script = music->GetComponent<Spot::ScriptComponent>();
    REQUIRE(audio);
    REQUIRE(script);
    REQUIRE(script->script);
    CHECK(script->script->sourceName == "scripts/music_fade.luau");
    CHECK(audio->volume < 0.05f);
    run(100);
    CHECK(audio->volume == Catch::Approx(0.35f));
    CHECK(audio->IsPlaying());
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::M, false});
    run(100);
    CHECK(audio->volume == 0.0f);
    CHECK(audio->IsPlaying());
    const auto capture = game.input.PushCapture("Luau test menu");
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::M, false});
    game.Step();
    CHECK(audio->volume == 0.0f);
    game.input.PopCapture(capture);
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::M, false});
    run(100);
    CHECK(audio->volume == Catch::Approx(0.35f));
    CHECK_FALSE(script->IsFaulted());
}

TEST_CASE("The sandbox Luau day controls drive the C++ cycle and real night lamps", "[sandbox][luau]") {
    SandboxTests::Playground game;
    auto* cycle = game.scene.FindComponent<DayCycle>();
    auto* control = game.scene.FindEntity("Game")->GetComponent<Spot::ScriptComponent>();
    auto* street = game.scene.FindEntity("Street Light");
    REQUIRE(cycle);
    REQUIRE(control);
    REQUIRE(control->script);
    REQUIRE(street);
    auto* lamp = street->GetComponent<Spot::PointLight>();
    REQUIRE(lamp);
    CHECK(control->script->sourceName == "scripts/day_controls.luau");
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::J, false});
    game.Step();
    CHECK(cycle->timeOfDay == Catch::Approx(0.30f).margin(0.002f));
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::L, false});
    game.Step();
    CHECK(cycle->timeOfDay == Catch::Approx(0.90f).margin(0.002f));
    CHECK(cycle->GetDaylight() < 0.01f);
    CHECK(lamp->IsEnabled());
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::K, false});
    game.Step();
    CHECK(cycle->timeOfDay == Catch::Approx(0.50f).margin(0.002f));
    CHECK(cycle->GetDaylight() > 0.99f);
    CHECK_FALSE(lamp->IsEnabled());
    const float runningLength = cycle->dayLength;
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::P, false});
    game.Step();
    const float pausedTime = cycle->timeOfDay;
    CHECK(cycle->dayLength == 0.0f);
    CHECK(cycle->IsActiveAndEnabled());
    game.Run(30);
    CHECK(cycle->timeOfDay == pausedTime);
    CHECK_FALSE(lamp->IsEnabled());
    game.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::P});
    game.Step();
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::P, false});
    game.Step();
    CHECK(cycle->dayLength == runningLength);
    game.Step();
    CHECK(cycle->timeOfDay > pausedTime);
    CHECK_FALSE(control->IsFaulted());
}
