#pragma once

#include "interface_settings.h"
#include "menu_thumbnails.h"
#include "spawnable.h"
#include "weapon_wheel.h"

#include <spot.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// What the HUD shows, which Hud works out from the game every frame.
struct HudState {
    // Frame rate and timings, shown only while statistics are on.
    std::string statistics;
    bool showStatistics = false;
    std::string clock;
    // Whether it is day, between 6:00 and 18:00, for the clock's sun or moon.
    bool day = true;
    // What is in hand: its icon, its name, shown a moment after switching (from 1 to 0 as it fades), a large line
    // (rounds left, or the tool's mode), a smaller one beside it (rounds in reserve, or the mode's option) and the
    // controls that matter for it.
    Spot::AssetRef<Spot::Texture> icon;
    std::string item;
    float itemOpacity = 0.0f;
    std::string count;
    std::string detail;
    std::string hints;
    // Large lines that are words rather than numbers are set smaller.
    bool countIsWord = false;
    bool lowAmmo = false;
    // From 0 to 1 while reloading; negative hides the bar.
    float reload = -1.0f;
    // Hidden down the sights, where the sights themselves aim, and while nothing can be used; spread from 0, steady,
    // to 1, as wide as it goes.
    bool crosshair = true;
    float spread = 0.0f;
    float hitMarker = 0.0f;
    // What Use would do with what is under the crosshair, such as picking it up; empty for nothing.
    std::string prompt;
    // The player's health out of its most; low health turns it red and pulses the edges of the screen, which flash as
    // it is hurt, as strongly as damage says, from 0 to 1.
    float health = 100.0f;
    float maxHealth = 100.0f;
    bool lowHealth = false;
    float damage = 0.0f;
    // Dead, with the seconds left before coming back.
    bool dead = false;
    float respawn = 0.0f;
    // The slots, in order, with the one in hand; while the wheel is open, the one it points at.
    std::vector<WeaponWheel::Slot> slots;
    int selectedSlot = 0;
    bool wheelOpen = false;
    int wheelSlot = 0;
};

class RuntimeInterface final : public Spot::Component {
public:
    enum class Menu { Closed, Spawn, Pause };
    Spot::AssetRef<Spot::Font> font;
    Spot::EntityId spawnCanvas = Spot::EntityId::Invalid, pauseCanvas = Spot::EntityId::Invalid;
    Spot::EntityId spawnGrid = Spot::EntityId::Invalid, spawnScroll = Spot::EntityId::Invalid,
                   spawnTemplate = Spot::EntityId::Invalid;
    Spot::EntityId propsTab = Spot::EntityId::Invalid, lampsTab = Spot::EntityId::Invalid,
                   weaponsTab = Spot::EntityId::Invalid;
    Spot::EntityId spawnClose = Spot::EntityId::Invalid, cleanup = Spot::EntityId::Invalid,
                   undoSpawn = Spot::EntityId::Invalid;
    Spot::EntityId pauseResume = Spot::EntityId::Invalid, pauseQuit = Spot::EntityId::Invalid;
    Spot::EntityId qualityButton = Spot::EntityId::Invalid, qualityLabel = Spot::EntityId::Invalid;
    Spot::EntityId bindingContent = Spot::EntityId::Invalid, bindingScroll = Spot::EntityId::Invalid,
                   bindingTemplate = Spot::EntityId::Invalid;
    Spot::EntityId sensitivitySlider = Spot::EntityId::Invalid, fovSlider = Spot::EntityId::Invalid;
    Spot::EntityId masterSlider = Spot::EntityId::Invalid, effectsSlider = Spot::EntityId::Invalid,
                   musicSlider = Spot::EntityId::Invalid;
    Spot::EntityId ambienceSlider = Spot::EntityId::Invalid, interfaceSlider = Spot::EntityId::Invalid;
    Spot::EntityId statusText = Spot::EntityId::Invalid, handText = Spot::EntityId::Invalid;
    Spot::EntityId statsPanel = Spot::EntityId::Invalid, clockText = Spot::EntityId::Invalid;
    Spot::EntityId countText = Spot::EntityId::Invalid, detailText = Spot::EntityId::Invalid,
                   hintText = Spot::EntityId::Invalid;
    Spot::EntityId reloadTrack = Spot::EntityId::Invalid, reloadFill = Spot::EntityId::Invalid;
    Spot::EntityId crosshair = Spot::EntityId::Invalid, hitMarker = Spot::EntityId::Invalid,
                   promptText = Spot::EntityId::Invalid, itemIcon = Spot::EntityId::Invalid;
    Spot::EntityId clockIcon = Spot::EntityId::Invalid;
    Spot::EntityId healthIcon = Spot::EntityId::Invalid, healthText = Spot::EntityId::Invalid,
                   healthFill = Spot::EntityId::Invalid;
    Spot::EntityId damageOverlay = Spot::EntityId::Invalid, deathOverlay = Spot::EntityId::Invalid,
                   deathTimer = Spot::EntityId::Invalid;
    Spot::EntityId wheelOverlay = Spot::EntityId::Invalid, weaponWheel = Spot::EntityId::Invalid;
    // What the clock shows beside the time by day and by night.
    Spot::AssetRef<Spot::Texture> sunIcon, moonIcon;
    // The reload and health bars' widths when full, in canvas pixels.
    float reloadWidth = 264.0f;
    float healthWidth = 100.0f;

    Menu GetMenu() const { return m_menu; }
    const InterfaceSettings& GetSettings() const { return m_settings; }
    void SetSettingsDirectory(std::filesystem::path directory);
    bool SaveSettings();
    void OpenSpawnMenu();
    void OpenPauseMenu();
    void CloseMenus();
    void SelectCategory(Spawnable::Category category);
    void UpdateHud(const HudState& state);
    std::span<const Spot::EntityId> GetSpawnButtons() const { return m_spawnButtons; }

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
    void OnDestroy() override;
    void Serialize(Spot::Archive& archive) override;

private:
    template <typename T>
    T* Get(Spot::EntityId id) const {
        auto* entity = GetScene().GetEntity(id);
        return entity ? entity->GetComponent<T>() : nullptr;
    }
    void Initialize();
    void Open(Menu menu);
    void BuildSpawnMenu();
    void BuildBindings();
    void BindSettings();
    void UpdateSettingsLabels();
    void UpdateBindingLabels();
    void SettingsChanged();
    void HandleRawEvent(const Spot::Event& event, bool& consumed);
    void ConnectButton(Spot::EntityId id, std::function<void()> callback);
    void RefreshInput();
    struct BindingRow {
        std::string action;
        std::size_t binding;
        int part;
        Spot::EntityId entity;
    };
    Menu m_menu = Menu::Closed;
    bool m_initialized = false;
    bool m_settingsDirty = false;
    bool m_paused = false;
    float m_previousTimeScale = 1;
    float m_saveDelay = 0;
    InterfaceSettings m_settings;
    std::filesystem::path m_settingsDirectory;
    std::vector<Spot::SignalConnection> m_connections;
    std::vector<Spot::EntityId> m_spawnButtons;
    std::vector<Spawnable::Category> m_categories;
    std::vector<BindingRow> m_bindingRows;
    int m_rebinding = -1;
    MenuThumbnails m_thumbnails;
    // The tabs' style as authored, which the selected one trades for the accent.
    std::optional<Spot::UiControlStyle> m_tabStyle;
    Spot::RenderHookId m_thumbnailHook = Spot::RenderHookId::Invalid;
};
