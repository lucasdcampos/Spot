#pragma once

#include "core/editor_settings.h"

#include <filesystem>
#include <functional>
#include <string>

namespace Spot::Editor {

// What the hub asks the editor to do.
struct ProjectHubActions {
    std::function<void(const std::filesystem::path& file)> openProject;
    std::function<void()> browseForProject;
    std::function<void()> newProject;
};

// The screen shown while no project is open: recent projects, and ways to create or open one.
class ProjectHub {
public:
    ProjectHub(EditorSettings& settings, ProjectHubActions actions);

    // Covers the whole window.
    void Draw();

private:
    void DrawRecentProjects();

    EditorSettings& m_settings;
    ProjectHubActions m_actions;
    std::string m_search;
};

} // namespace Spot::Editor
