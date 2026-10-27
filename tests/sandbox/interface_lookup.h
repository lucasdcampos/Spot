#pragma once

#include "gameplay/game/death_screen_element.h"

#include "sandbox_hud.h"
#include "spawn_menu.h"

#include "gameplay/character/character_elements.h"
#include "gameplay/environment/clock_element.h"
#include "gameplay/health/health_elements.h"
#include "gameplay/hud/hud_elements.h"
#include "gameplay/inventory/inventory_elements.h"
#include "gameplay/menus/help_panel.h"
#include "gameplay/menus/pause_menu.h"
#include "gameplay/menus/settings_menu.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

namespace SandboxTests {

// The sandbox's authored interface, by what its menus and HUD elements draw into.
struct Interface {
    Spot::Scene& scene;
    SpawnMenu& spawn = Require<SpawnMenu>();
    Gameplay::PauseMenu& pause = Require<Gameplay::PauseMenu>();
    Gameplay::SettingsMenu& settings = Require<Gameplay::SettingsMenu>();
    Gameplay::BindingsMenu& bindings = Require<Gameplay::BindingsMenu>();
    Gameplay::ItemPanelElement& hand = Require<Gameplay::ItemPanelElement>();
    Gameplay::CrosshairElement& crosshairElement = Require<Gameplay::CrosshairElement>();
    Gameplay::StatisticsElement& statistics = Require<Gameplay::StatisticsElement>();
    Gameplay::ClockElement& clock = Require<Gameplay::ClockElement>();
    Gameplay::HealthElement& health = Require<Gameplay::HealthElement>();
    Gameplay::DeathScreenElement& death = Require<Gameplay::DeathScreenElement>();
    Gameplay::HelpPanel& help = Require<Gameplay::HelpPanel>();
    Gameplay::WheelElement& wheel = Require<Gameplay::WheelElement>();

    Spot::EntityId handText = hand.name, countText = hand.count, detailText = hand.detail, hintText = hand.hints,
                   itemIcon = hand.icon, reloadTrack = hand.reloadTrack, reloadFill = hand.reloadFill;
    Spot::EntityId crosshair = crosshairElement.GetEntity().GetId();
    Spot::EntityId promptText = Require<SandboxPromptElement>().GetEntity().GetId();
    Spot::EntityId statsPanel = statistics.GetEntity().GetId(), statusText = statistics.text;
    Spot::EntityId clockText = clock.text, clockIcon = clock.icon;
    Spot::AssetRef<Spot::Texture> sunIcon = clock.sunIcon;
    Spot::EntityId healthText = health.text;
    Spot::EntityId damageOverlay = Require<Gameplay::DamageFlashElement>().GetEntity().GetId();
    Spot::EntityId deathOverlay = death.GetEntity().GetId(), deathTimer = death.timer;
    Spot::EntityId helpPanel = help.GetEntity().GetId(), helpContent = help.rows, helpHint = help.hint;
    Spot::EntityId propsTab = spawn.propsTab, lampsTab = spawn.lampsTab, weaponsTab = spawn.weaponsTab;
    Spot::EntityId spawnCount = spawn.count, spawnSearch = spawn.search, spawnGrid = spawn.grid;
    Spot::EntityId gridView = spawn.gridView, undoSpawn = spawn.undoButton, cleanup = spawn.cleanupButton;
    Spot::EntityId sensitivitySlider = settings.sensitivitySlider, fovSlider = settings.fieldOfViewSlider,
                   masterSlider = settings.masterSlider, qualityButton = settings.qualityButton;
    Spot::EntityId bindingContent = bindings.rows;
    Spot::EntityId wheelOverlay = wheel.GetEntity().GetId(), weaponWheel = wheel.wheel;

    // Whether no menu is open.
    bool IsClosed() const { return !Gameplay::Menu::GetOpen(scene); }
    std::string GetText(Spot::EntityId id) const {
        const Spot::Entity* entity = scene.GetEntity(id);
        REQUIRE(entity);
        return entity->GetComponent<Spot::UiText>()->text;
    }

private:
    template <typename T>
    // Hidden parts of the HUD are on inactive entities, which FindComponent() passes by.
    T& Require() {
        T* found = nullptr;
        scene.ForEachEntity([&](Spot::Entity& entity) {
            if (!found) {
                found = entity.GetComponent<T>();
            }
        });
        REQUIRE(found);
        return *found;
    }
};

} // namespace SandboxTests
