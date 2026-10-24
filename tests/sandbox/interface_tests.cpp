#include "../registered_assets.h"
#include "hud.h"
#include "interface_settings.h"
#include "log_capture.h"
#include "player_controller.h"
#include "prop_spawner.h"
#include "runtime_interface.h"
#include "spot/scene/ui/ui_input_system.h"
#include "spot/scene/ui/ui_system.h"
#include "temp_directory.h"
#include "undo_list.h"
#include "weapon.h"
#include "weapon_holder.h"

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

namespace {
struct InterfaceGame {
    std::shared_ptr<Spot::Tests::CaptureSink> log = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks{{log}};
    Spot::InputState input;
    Spot::InputActionMap actions;
    Spot::Tests::RegisteredAssetManager assets{SANDBOX_IMPORTED_DIR};
    Spot::InputState* previousInput = Spot::Input::GetState();
    Spot::InputActionMap* previousActions = Spot::Input::GetActionMap();
    Spot::AssetManager* previousAssets = Spot::Assets::GetManager();
    float previousScale = Spot::Time::GetTimeScale();
    Spot::Scene scene;
    InterfaceGame() {
        Spot::Time::SetTimeScale(1);
        Spot::Detail::GameCode::Load();
        Spot::Input::SetState(&input);
        Spot::Input::SetActionMap(&actions);
        Spot::Detail::SetAssetManager(&assets);
        const auto game =
            Spot::GameSettings::LoadFromFile(std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() / "game.json");
        REQUIRE(game);
        game->sections.input.Apply(actions);
        actions.SetDefaults();
        const auto snapshot = Spot::Process::GetEnvironmentValue("SPOT_SANDBOX_TEST_ASSETS");
        const auto path = snapshot && !snapshot->empty() ? std::filesystem::path(*snapshot)
                                                         : std::filesystem::path(SANDBOX_ASSETS_DIR);
        REQUIRE(scene.LoadFromFile(path / "scenes/playground.scene", &assets));
        Step();
        Step();
        Step();
        REQUIRE(scene.FindComponent<RuntimeInterface>());
    }
    ~InterfaceGame() {
        scene.Clear();
        Spot::Time::SetTimeScale(previousScale);
        Spot::Input::SetState(previousInput);
        Spot::Input::SetActionMap(previousActions);
        Spot::Detail::SetAssetManager(previousAssets);
        for (const auto& record : log->GetRecords())
            CHECK(record.message.find(" threw in ") == std::string::npos);
    }
    RuntimeInterface& GetUi() { return *scene.FindComponent<RuntimeInterface>(); }
    void Step() {
        actions.Update(input);
        scene.Update(1.0f / 60 * Spot::Time::GetTimeScale());
        input.BeginFrame();
    }
    bool Send(const Spot::Event& event) {
        const bool used = scene.GetOrAddSystem<Spot::UiInputSystem>().HandleUiEvent(event, {1280, 720}, &input);
        if (!used)
            input.OnEvent(event);
        return used;
    }
};
} // namespace

TEST_CASE("Sandbox authored HUD and spawn menu route Q and gamepad tabs", "[sandbox][ui]") {
    InterfaceGame game;
    auto& ui = game.GetOrAddSystem<Spot::UiSystem>();
    CHECK(game.scene.GetEntity(ui.handText)->GetComponent<Spot::UiText>()->text == "PHYSICS GUN");
    CHECK(game.scene.GetEntity(ui.statusText)->GetComponent<Spot::UiText>()->text.find("bodies") != std::string::npos);
    REQUIRE(ui.GetSpawnButtons().size() == game.scene.FindComponent<PropSpawner>()->GetSpawnables().size());
    CHECK(game.Send(Spot::KeyPressedEvent{Spot::Key::Q, false}));
    game.Step();
    CHECK(ui.GetMenu() == RuntimeInterface::Menu::Spawn);
    CHECK(game.input.IsCaptured());
    game.Send(Spot::KeyReleasedEvent{Spot::Key::Q});
    const auto* first = game.scene.GetEntity(ui.GetSpawnButtons().front());
    REQUIRE(first);
    CHECK(first->GetComponent<Spot::UiButton>());
    CHECK(first->FindChild("Thumbnail")->GetComponent<Spot::UiImage>());
    game.scene.GetOrAddSystem<Spot::UiInputSystem>().Focus(ui.weaponsTab);
    game.Send(Spot::GamepadButtonPressedEvent{Spot::GamepadId{1}, Spot::GamepadButton::South});
    CHECK_FALSE(first->IsActiveInHierarchy());
    CHECK(game.scene.GetOrAddSystem<Spot::UiInputSystem>().GetFocusedEntity() == ui.weaponsTab);
    CHECK(game.Send(Spot::KeyPressedEvent{Spot::Key::Q, false}));
    game.Step();
    CHECK(ui.GetMenu() == RuntimeInterface::Menu::Closed);
    CHECK_FALSE(game.input.IsCaptured());
    CHECK(game.Send(Spot::KeyReleasedEvent{Spot::Key::Q}));
}

TEST_CASE("The sandbox HUD shows rounds, reloads, sights, the item's name and statistics like a game",
          "[sandbox][ui][hud]") {
    InterfaceGame game;
    auto& ui = game.GetOrAddSystem<Spot::UiSystem>();
    const auto text = [&](Spot::EntityId id) { return game.scene.GetEntity(id)->GetComponent<Spot::UiText>()->text; };
    const auto active = [&](Spot::EntityId id) { return game.scene.GetEntity(id)->IsActiveInHierarchy(); };
    const auto nameOpacity = [&] { return game.scene.GetEntity(ui.handText)->GetComponent<Spot::UiText>()->color.a; };
    // Debugging statistics wait for their key; the name of what is in hand for a switch.
    CHECK_FALSE(active(ui.statsPanel));
    CHECK(nameOpacity() == 0.0f);
    CHECK(active(ui.crosshair));
    CHECK(text(ui.clockText).size() == 5);
    // By day the clock shows the sun.
    CHECK(game.scene.GetEntity(ui.clockIcon)->GetComponent<Spot::UiImage>()->texture == ui.sunIcon);

    auto& holder = *game.scene.FindComponent<WeaponHolder>();
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::Digit2, false});
    game.Step();
    game.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::Digit2});
    game.Step();
    REQUIRE(holder.GetWeapon());
    CHECK(nameOpacity() == 1.0f);
    CHECK(text(ui.handText) == "PISTOL");
    CHECK(text(ui.countText) == std::to_string(holder.GetWeapon()->GetAmmo()));
    CHECK(text(ui.detailText) == "/ " + std::to_string(holder.GetWeapon()->GetReserve()));
    for (int i = 0; i < 180; ++i)
        game.Step();
    CHECK(nameOpacity() == 0.0f);

    // Down the sights, the crosshair gives way to them.
    game.input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Right});
    game.Step();
    CHECK_FALSE(active(ui.crosshair));
    game.input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Right});
    game.Step();
    CHECK(active(ui.crosshair));

    // Reloading fills a bar.
    holder.Fire();
    REQUIRE(holder.GetWeapon()->StartReload());
    for (int i = 0; i < 30; ++i)
        game.Step();
    CHECK(active(ui.reloadTrack));
    CHECK(game.scene.GetEntity(ui.reloadFill)->GetComponent<Spot::UiRect>()->layout.sizeDelta.x > 0.0f);
    for (int i = 0; i < 120; ++i)
        game.Step();
    CHECK_FALSE(active(ui.reloadTrack));

    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::F11, false});
    game.Step();
    game.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::F11});
    game.Step();
    CHECK(active(ui.statsPanel));
    CHECK(text(ui.statusText).find("FPS") != std::string::npos);
    CHECK(game.scene.FindComponent<Hud>()->IsShowingStatistics());
}

TEST_CASE("Sandbox menu clicks spawn real prefabs and offer undo and cleanup", "[sandbox][ui]") {
    InterfaceGame game;
    auto& ui = game.GetOrAddSystem<Spot::UiSystem>();
    ui.OpenSpawnMenu();
    auto& spawner = *game.scene.FindComponent<PropSpawner>();
    auto* button = game.scene.GetEntity(ui.GetSpawnButtons().front())->GetComponent<Spot::UiButton>();
    REQUIRE(button);
    const auto* layout = game.scene.GetOrAddSystem<Spot::UiSystem>().Find(button->GetEntity().GetId());
    REQUIRE(layout);
    const auto center = layout->bounds.GetCenter();
    game.Send(Spot::MouseMovedEvent{{center.x, 720 - center.y}, {}});
    CHECK(game.Send(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left}));
    CHECK(game.Send(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left}));
    CHECK(spawner.GetSpawnedCount() == 1);
    CHECK_FALSE(game.input.IsMouseButtonPressed(Spot::MouseButton::Left));
    game.scene.GetEntity(ui.undoSpawn)->GetComponent<Spot::UiButton>()->Click();
    CHECK(spawner.GetSpawnedCount() == 0);
    button->Click();
    CHECK(spawner.GetSpawnedCount() == 1);
    game.scene.GetEntity(ui.cleanup)->GetComponent<Spot::UiButton>()->Click();
    CHECK(spawner.GetSpawnedCount() == 0);
}

TEST_CASE("Sandbox pause settings persist sensitivity FOV volumes quality and bindings", "[sandbox][ui]") {
    InterfaceGame game;
    TempDirectory directory;
    auto& ui = game.GetOrAddSystem<Spot::UiSystem>();
    ui.SetSettingsDirectory(directory.GetPath());
    Spot::Time::SetTimeScale(0.5f);
    ui.OpenPauseMenu();
    CHECK(Spot::Time::GetTimeScale() == 0);
    CHECK(game.input.IsCaptured());
    game.scene.GetEntity(ui.sensitivitySlider)->GetComponent<Spot::UiSlider>()->SetValue(0.004f, true);
    game.scene.GetEntity(ui.fovSlider)->GetComponent<Spot::UiSlider>()->SetValue(90, true);
    game.scene.GetEntity(ui.masterSlider)->GetComponent<Spot::UiSlider>()->SetValue(0.4f, true);
    game.scene.GetEntity(ui.qualityButton)->GetComponent<Spot::UiButton>()->Click();
    CHECK(ui.GetSettings().graphicsQuality == 0);
    const auto* player = game.scene.FindComponent<PlayerController>();
    REQUIRE(player);
    CHECK(player->lookSensitivity == Catch::Approx(0.004f));
    CHECK(Spot::ToDegrees(player->GetFieldOfView()) == Catch::Approx(90));
    auto* rows = game.scene.GetEntity(ui.bindingContent);
    REQUIRE(rows);
    Spot::UiButton* jump = nullptr;
    rows->ForEachChild([&](Spot::Entity& child) {
        if (!jump && child.GetName() == "Binding Jump")
            jump = child.GetComponent<Spot::UiButton>();
    });
    REQUIRE(jump);
    jump->Click();
    CHECK(game.Send(Spot::KeyPressedEvent{Spot::Key::J, false}));
    CHECK(game.actions.Find("Jump")->bindings.front().control == Spot::InputControl::Of(Spot::Key::J));
    CHECK_FALSE(game.input.IsKeyPressed(Spot::Key::J));
    game.Send(Spot::KeyPressedEvent{Spot::Key::Escape, false});
    CHECK(ui.GetMenu() == RuntimeInterface::Menu::Closed);
    CHECK(Spot::Time::GetTimeScale() == 0.5f);
    CHECK_FALSE(game.input.IsCaptured());
    InterfaceSettings loaded;
    REQUIRE(loaded.Load(directory.GetPath()));
    CHECK(loaded.sensitivity == Catch::Approx(0.004f));
    CHECK(loaded.fieldOfView == 90);
    CHECK(loaded.masterVolume == Catch::Approx(0.4f));
    CHECK(loaded.graphicsQuality == 0);
    Spot::InputActionMap reloaded;
    reloaded.Add("Jump", Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::Space));
    reloaded.SetDefaults();
    REQUIRE(reloaded.LoadRebindsFromString(loaded.rebinds));
    CHECK(reloaded.Find("Jump")->bindings.front().control == Spot::InputControl::Of(Spot::Key::J));
}

TEST_CASE("Interface settings validate data and preserve previous values on failed loads", "[sandbox][ui]") {
    TempDirectory directory;
    InterfaceSettings settings;
    settings.sensitivity = std::numeric_limits<float>::quiet_NaN();
    settings.fieldOfView = 500;
    settings.masterVolume = -1;
    settings.graphicsQuality = 99;
    REQUIRE(settings.Save(directory.GetPath()));
    InterfaceSettings loaded;
    REQUIRE(loaded.Load(directory.GetPath()));
    CHECK(std::isfinite(loaded.sensitivity));
    CHECK(loaded.fieldOfView == 120);
    CHECK(loaded.masterVolume == 0);
    CHECK(loaded.graphicsQuality == 2);
    loaded.fieldOfView = 80;
    Spot::SaveData store(directory.GetPath());
    REQUIRE(store.Save("interface.json", 99, [](Spot::Archive&, std::uint32_t) {}));
    CHECK_FALSE(loaded.Load(directory.GetPath()));
    CHECK(loaded.fieldOfView == 80);
}
