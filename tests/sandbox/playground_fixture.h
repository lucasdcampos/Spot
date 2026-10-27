#include "../registered_assets.h"
#pragma once

#include "log_capture.h"
#include "temp_directory.h"

#include "gameplay/inventory/held_item.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/menus/pause_menu.h"
#include "gameplay/menus/settings_menu.h"
#include "gameplay/weapons/firearm.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace SandboxTests {

// The firearm in hand; null when what is in hand is not one.
inline Gameplay::Firearm* GetFirearm(const Gameplay::Inventory& inventory) {
    return dynamic_cast<Gameplay::Firearm*>(inventory.GetEquipped());
}

// The inventory's firearms, in their order.
inline std::vector<Gameplay::Firearm*> GetFirearms(const Gameplay::Inventory& inventory) {
    std::vector<Gameplay::Firearm*> firearms;
    for (Gameplay::HeldItem* item : inventory.GetItems()) {
        if (auto* firearm = dynamic_cast<Gameplay::Firearm*>(item)) {
            firearms.push_back(firearm);
        }
    }
    return firearms;
}

// The first of the inventory's items of the type; null when it has none.
template <typename T>
T* GetItem(const Gameplay::Inventory& inventory) {
    for (Gameplay::HeldItem* item : inventory.GetItems()) {
        if (auto* found = dynamic_cast<T*>(item)) {
            return found;
        }
    }
    return nullptr;
}

// Whether the item of the type is in hand.
template <typename T>
bool IsEquipped(const Gameplay::Inventory& inventory) {
    const T* item = GetItem<T>(inventory);
    return item && inventory.GetEquipped() == item;
}

// Fires the firearm in hand, if it is one and it can.
inline void Fire(const Gameplay::Inventory& inventory) {
    if (Gameplay::Firearm* firearm = GetFirearm(inventory)) {
        firearm->Fire();
    }
}

struct Playground {
    std::shared_ptr<Spot::Tests::CaptureSink> log = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks{{log}};
    TempDirectory userData;
    Spot::InputState input;
    Spot::InputActionMap actions;
    Spot::Tests::RegisteredAssetManager assets{SANDBOX_IMPORTED_DIR};
    Spot::InputState* previousInput = Spot::Input::GetState();
    Spot::InputActionMap* previousActions = Spot::Input::GetActionMap();
    Spot::AssetManager* previousAssets = Spot::Assets::GetManager();
    Spot::AudioDevice* previousAudio = Spot::Audio::GetDevice();
    std::unique_ptr<Spot::AudioDevice> audio;
    float previousScale = Spot::Time::GetTimeScale();
    Spot::Scene scene;

    explicit Playground(bool withAudio = false) {
        Spot::Detail::LoadGameCode();
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
        auto* settings = scene.FindComponent<Gameplay::SettingsMenu>();
        REQUIRE(settings);
        settings->SetSettingsDirectory(userData.GetPath());
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
