#pragma once

#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>

namespace GameplayTests {

// A bare world for testing a system of the gameplay framework alone: a ground whose top is at y = 0 and nothing
// else, input the test presses on, read through the actions the test defines, and the game's components registered,
// as the game loads them. A system's tests add only its own components and the engine's, which shows it needs no
// other.
struct World {
    std::shared_ptr<Spot::Tests::CaptureSink> log = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks{{log}};
    Spot::InputState input;
    Spot::InputActionMap actions;
    Spot::InputState* previousInput = Spot::Input::GetState();
    Spot::InputActionMap* previousActions = Spot::Input::GetActionMap();
    Spot::Scene scene;
    Spot::Entity& ground = CreateGround(scene);

    World() {
        Spot::Detail::LoadGameCode();
        Spot::Input::SetState(&input);
        Spot::Input::SetActionMap(&actions);
    }

    ~World() {
        scene.Clear();
        Spot::Input::SetState(previousInput);
        Spot::Input::SetActionMap(previousActions);
        // Components that threw were logged; nothing should have.
        for (const auto& record : log->GetRecords()) {
            INFO(record.message);
            CHECK(record.message.find(" threw in ") == std::string::npos);
        }
    }

    void Step(float seconds = Spot::Scene::DefaultFixedTimeStep) {
        actions.Update(input);
        scene.Update(seconds);
        input.BeginFrame();
    }
    void Run(float seconds) {
        for (long i = std::lround(seconds / Spot::Scene::DefaultFixedTimeStep); i > 0; --i) {
            Step();
        }
    }

private:
    static Spot::Entity& CreateGround(Spot::Scene& scene) {
        auto& ground = scene.CreateEntity("Ground");
        ground.GetTransform().SetPosition({0.0f, -0.5f, 0.0f});
        ground.AddComponent<Spot::BoxCollider>(Spot::Vector3{100.0f, 1.0f, 100.0f});
        return ground;
    }
};

} // namespace GameplayTests
