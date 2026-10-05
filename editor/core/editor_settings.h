#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Spot::Editor {

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

private:
    std::filesystem::path m_file;
    std::vector<RecentProject> m_recentProjects;
    std::filesystem::path m_projectsLocation;
};

// "<documents>/Spot Projects", where projects go until the user picks another place.
std::filesystem::path GetDefaultProjectsLocation();

} // namespace Spot::Editor
