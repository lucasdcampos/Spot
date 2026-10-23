#pragma once

#include "log_capture.h"
#include "runtime_interface.h"
#include "temp_directory.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

namespace SandboxTests {

struct Playground {
    std::shared_ptr<Spot::Tests::CaptureSink> log = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks{{log}};
    TempDirectory userData;
    Spot::InputState input;
    Spot::InputActionMap actions;
    Spot::AssetManager assets{SANDBOX_IMPORTED_DIR};
    Spot::InputState* previousInput = Spot::Input::GetState();
    Spot::InputActionMap* previousActions = Spot::Input::GetActionMap();
    Spot::AssetManager* previousAssets = Spot::Assets::GetManager();
    Spot::AudioDevice* previousAudio = Spot::Audio::GetDevice();
    std::unique_ptr<Spot::AudioDevice> audio;
    float previousScale = Spot::Time::GetTimeScale();
    Spot::Scene scene;

    explicit Playground(bool withAudio = false) {
        Spot::Detail::GameCode::Load();
        Spot::Time::SetTimeScale(1);
        Spot::Input::SetState(&input);
        Spot::Input::SetActionMap(&actions);
        Spot::Detail::SetAssetManager(&assets);
        const auto game = Spot::GameSettings::LoadFromFile(std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() /
                                                           Spot::GameSettings::FileName);
        REQUIRE(game);
        if (withAudio) {
            audio = std::make_unique<Spot::AudioDevice>(Spot::AudioConfig{.manualMix = true});
            game->sections.audio.Apply(*audio);
            Spot::Audio::SetDevice(audio.get());
        }
        game->sections.input.Apply(actions);
        actions.SetDefaults();
        const auto snapshot = Spot::Process::GetEnvironmentValue("SPOT_SANDBOX_TEST_ASSETS");
        const auto sources = snapshot && !snapshot->empty() ? std::filesystem::path(*snapshot)
                                                            : std::filesystem::path(SANDBOX_ASSETS_DIR);
        REQUIRE(scene.LoadFromFile(sources / "scenes/playground.scene", &assets));
        Step();
        auto* ui = scene.FindComponent<RuntimeInterface>();
        REQUIRE(ui);
        ui->SetSettingsDirectory(userData.GetPath());
        Step();
    }

    ~Playground() {
        scene.Clear();
        Spot::Input::SetState(previousInput);
        Spot::Input::SetActionMap(previousActions);
        Spot::Detail::SetAssetManager(previousAssets);
        Spot::Audio::SetDevice(previousAudio);
        Spot::Time::SetTimeScale(previousScale);
        for (const auto& record : log->GetRecords()) {
            INFO(record.message);
            CHECK(record.message.find(" threw in ") == std::string::npos);
        }
    }

    void Step(float seconds = Spot::Scene::DefaultFixedTimeStep) {
        actions.Update(input);
        scene.Update(seconds * Spot::Time::GetTimeScale());
        input.BeginFrame();
    }
    void Run(int frames) {
        for (int i = 0; i < frames; ++i) {
            Step();
        }
    }
};

} // namespace SandboxTests
