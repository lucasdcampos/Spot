#pragma once

#include "interface_settings.h"
#include "menu_thumbnails.h"
#include "spawnable.h"

#include <spot.h>

#include <filesystem>
#include <string>
#include <vector>

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

    Menu GetMenu() const { return m_menu; }
    const InterfaceSettings& GetSettings() const { return m_settings; }
    void SetSettingsDirectory(std::filesystem::path directory);
    bool SaveSettings();
    void OpenSpawnMenu();
    void OpenPauseMenu();
    void CloseMenus();
    void SelectCategory(Spawnable::Category category);
    void SetHudText(std::string_view status, std::string_view hand);
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
    Spot::RenderHookId m_thumbnailHook = Spot::RenderHookId::Invalid;
};
