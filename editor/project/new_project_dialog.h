#pragma once

#include "core/editor_settings.h"
#include "project/project_templates.h"

#include <spot.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace Spot::Editor {

// A modal dialog that asks for a project's name, folder and template, and creates it.
class NewProjectDialog {
public:
    NewProjectDialog(EditorSettings& settings, Window& window);

    void Open();
    // Draws the dialog while it is open. Returns the new project's file the frame it is created.
    std::optional<std::filesystem::path> Draw();

private:
    // Where the folder chosen in the OS dialog lands, since the dialog answers later, possibly after this one closed.
    struct ChosenLocation {
        std::optional<std::filesystem::path> path;
    };

    EditorSettings& m_settings;
    Window& m_window;
    bool m_openRequested = false;
    std::string m_name;
    std::string m_location;
    ProjectTemplate m_template = ProjectTemplate::Basic3D;
    std::string m_error;
    std::shared_ptr<ChosenLocation> m_chosenLocation = std::make_shared<ChosenLocation>();
};

} // namespace Spot::Editor
