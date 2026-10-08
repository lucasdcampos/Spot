#pragma once

#include "core/editor_settings.h"

#include <spot.h>

#include <filesystem>
#include <memory>
#include <optional>

namespace Spot::Editor {

// The editor's preferences, in a window of their own. Changes apply at once and are saved as they are made.
class PreferencesWindow {
public:
    PreferencesWindow(EditorSettings& settings, Window& window);

    void Open() { m_open = true; }
    bool IsOpen() const { return m_open; }
    void Draw();

private:
    // Where the folder chosen in the OS dialog lands, since the dialog answers later, possibly after this closed.
    struct ChosenLocation {
        std::optional<std::filesystem::path> path;
    };

    EditorSettings& m_settings;
    Window& m_window;
    bool m_open = false;
    std::shared_ptr<ChosenLocation> m_chosenLocation = std::make_shared<ChosenLocation>();
};

} // namespace Spot::Editor
