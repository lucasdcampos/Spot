#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Spot::Editor {

// What dragging in a scene view does to the selected entities.
enum class GizmoTool {
    Select,
    Move,
    Rotate,
    Scale,
};

// How the transform gizmo works, as the toolbar sets it.
struct GizmoSettings {
    GizmoTool tool = GizmoTool::Move;
    // Moves and turns along the entity's own axes instead of the world's.
    bool local = false;
    // Moves, turns and scales in steps; holding Ctrl while dragging does the opposite.
    bool snap = false;
    float moveStep = 0.5f;
    // In degrees.
    float rotateStep = 15.0f;
    float scaleStep = 0.1f;
};

struct RecentProject {
    std::filesystem::path file;
    std::string name;
    std::chrono::system_clock::time_point lastOpened;
};

// The user's editor settings, kept between runs in a JSON file of the user's data directory.
class EditorSettings {
public:
    static constexpr std::size_t MaxRecentProjects = 12;

    // An empty file keeps the settings in memory only.
    explicit EditorSettings(std::filesystem::path file = {});

    // Missing or unreadable files leave the defaults, after logging why when the file exists.
    void Load();
    bool Save() const;

    // Most recently opened first.
    const std::vector<RecentProject>& GetRecentProjects() const { return m_recentProjects; }
    // Moves the project to the front, adding it if new and dropping the oldest beyond the limit.
    void AddRecentProject(const std::filesystem::path& file, std::string name,
                          std::chrono::system_clock::time_point opened = std::chrono::system_clock::now());
    void RemoveRecentProject(const std::filesystem::path& file);

    // Where new projects are created by default.
    const std::filesystem::path& GetProjectsLocation() const { return m_projectsLocation; }
    void SetProjectsLocation(std::filesystem::path location) { m_projectsLocation = std::move(location); }

    GizmoSettings& GetGizmo() { return m_gizmo; }
    const GizmoSettings& GetGizmo() const { return m_gizmo; }

private:
    std::filesystem::path m_file;
    std::vector<RecentProject> m_recentProjects;
    std::filesystem::path m_projectsLocation;
    GizmoSettings m_gizmo;
};

// "<documents>/Spot Projects", where projects go until the user picks another place.
std::filesystem::path GetDefaultProjectsLocation();

} // namespace Spot::Editor
