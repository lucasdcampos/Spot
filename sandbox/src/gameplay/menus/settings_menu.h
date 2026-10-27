#pragma once

#include "gameplay/core/user_settings.h"
#include "gameplay/menus/menu.h"

#include <spot.h>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Gameplay {

// The settings part of a menu: sliders for the look and the volumes, and a button that cycles the graphics quality.
// It loads the player's UserSettings from the user's data as the menu starts, applies them as they change, and saves
// them a moment after, or as the menu closes. A slider's value shows in the "Value" text beside it.
class SettingsMenu : public Spot::Component, public MenuPart {
public:
    Spot::EntityId sensitivitySlider = Spot::EntityId::Invalid;
    Spot::EntityId fieldOfViewSlider = Spot::EntityId::Invalid;
    Spot::EntityId masterSlider = Spot::EntityId::Invalid;
    Spot::EntityId effectsSlider = Spot::EntityId::Invalid;
    Spot::EntityId musicSlider = Spot::EntityId::Invalid;
    Spot::EntityId ambienceSlider = Spot::EntityId::Invalid;
    Spot::EntityId interfaceSlider = Spot::EntityId::Invalid;
    Spot::EntityId qualityButton = Spot::EntityId::Invalid;
    Spot::EntityId qualityLabel = Spot::EntityId::Invalid;

    const UserSettings& GetSettings() const { return m_settings; }
    // Where the settings are kept; the application's user data by default.
    void SetSettingsDirectory(std::filesystem::path directory) { m_directory = std::move(directory); }
    bool Save();
    // Applies the settings as they are now, shows them and saves them shortly, as after a control was rebound.
    void MarkChanged();

protected:
    void OnMenuInitialize(Menu& menu) override;
    void OnMenuClose(Menu& menu) override;
    void AnimateMenu(Menu& menu, float blend) override;
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    void UpdateLabels();

    UserSettings m_settings;
    std::filesystem::path m_directory;
    std::vector<Spot::SignalConnection> m_connections;
    bool m_dirty = false;
    float m_saveDelay = 0.0f;
};

// The bindings part of a menu: a row for each binding of the action map, made from a template, that shows its control;
// clicking one waits for the control to bind in its place, and Escape stops waiting. The settings part keeps the
// rebinds.
class BindingsMenu : public Spot::Component, public MenuPart {
public:
    Spot::EntityId rows = Spot::EntityId::Invalid;
    Spot::EntityId scroll = Spot::EntityId::Invalid;
    Spot::EntityId rowTemplate = Spot::EntityId::Invalid;
    Spot::AssetRef<Spot::Font> font;
    // For the controls; the font when unset.
    Spot::AssetRef<Spot::Font> monoFont;
    // What a row is in canvas pixels.
    Spot::Vector2 rowSize = {412.0f, 28.0f};

    bool IsRebinding() const { return m_rebinding >= 0; }
    std::span<const Spot::EntityId> GetRows() const { return m_rowEntities; }

protected:
    void OnMenuInitialize(Menu& menu) override;
    void OnMenuOpen(Menu& menu) override;
    void OnMenuClose(Menu& menu) override;
    void AnimateMenu(Menu& menu, float blend) override;
    void OnMenuRawEvent(Menu& menu, const Spot::Event& event, bool& consumed) override;
    bool IsBusy() const override { return IsRebinding(); }
    void Serialize(Spot::Archive& archive) override;

private:
    struct Row {
        std::string action;
        std::size_t binding;
        int part;
        Spot::EntityId entity;
    };
    void UpdateLabels();

    std::vector<Row> m_rows;
    std::vector<Spot::EntityId> m_rowEntities;
    int m_rebinding = -1;
    Spot::Color m_accent;
    Spot::Color m_text;
    std::vector<Spot::SignalConnection> m_connections;
};

} // namespace Gameplay
