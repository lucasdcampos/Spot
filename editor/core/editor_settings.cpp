#include "core/editor_settings.h"

#include "core/editor_log.h"

#include <spot.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>

namespace Spot::Editor {

using nlohmann::json;

namespace {

constexpr std::array<std::string_view, 4> GizmoToolNames{"Select", "Move", "Rotate", "Scale"};

bool IsSameFile(const std::filesystem::path& a, const std::filesystem::path& b) {
    return a.lexically_normal() == b.lexically_normal();
}

} // namespace

EditorSettings::EditorSettings(std::filesystem::path file)
    : m_file(std::move(file)), m_projectsLocation(GetDefaultProjectsLocation()) {
}

void EditorSettings::Load() {
    if (m_file.empty() || !std::filesystem::exists(m_file)) {
        return;
    }
    std::ifstream stream(m_file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const json document = json::parse(text, nullptr, false);
    if (!document.is_object()) {
        GetEditorLogger().Warning("The editor settings in '{}' are not valid JSON; using defaults", PathToUtf8(m_file));
        return;
    }

    m_recentProjects.clear();
    if (const auto recent = document.find("recentProjects"); recent != document.end() && recent->is_array()) {
        for (const json& entry : *recent) {
            if (!entry.is_object() || !entry.contains("file") || !entry["file"].is_string()) {
                continue;
            }
            RecentProject project;
            project.file = Utf8ToPath(entry["file"].get<std::string>());
            project.name = entry.value("name", PathToUtf8(project.file.stem()));
            project.lastOpened =
                std::chrono::system_clock::time_point(std::chrono::seconds(entry.value("lastOpened", std::int64_t{0})));
            m_recentProjects.push_back(std::move(project));
        }
    }
    if (const auto location = document.find("projectsLocation"); location != document.end() && location->is_string()) {
        m_projectsLocation = Utf8ToPath(location->get<std::string>());
    }
    if (const auto gizmo = document.find("gizmo"); gizmo != document.end() && gizmo->is_object()) {
        const auto tool = std::ranges::find(GizmoToolNames, gizmo->value("tool", std::string()));
        if (tool != GizmoToolNames.end()) {
            m_gizmo.tool = static_cast<GizmoTool>(tool - GizmoToolNames.begin());
        }
        m_gizmo.local = gizmo->value("local", m_gizmo.local);
        m_gizmo.snap = gizmo->value("snap", m_gizmo.snap);
        // Steps of nothing would never move.
        const auto step = [&](const char* name, float fallback) {
            const float value = gizmo->value(name, fallback);
            return value > 0.0f ? value : fallback;
        };
        m_gizmo.moveStep = step("moveStep", m_gizmo.moveStep);
        m_gizmo.rotateStep = step("rotateStep", m_gizmo.rotateStep);
        m_gizmo.scaleStep = step("scaleStep", m_gizmo.scaleStep);
    }
    if (const auto preferences = document.find("preferences");
        preferences != document.end() && preferences->is_object()) {
        m_preferences.uiScale = std::clamp(preferences->value("uiScale", m_preferences.uiScale),
                                           EditorPreferences::MinUiScale, EditorPreferences::MaxUiScale);
        m_preferences.clearConsoleOnPlay = preferences->value("clearConsoleOnPlay", m_preferences.clearConsoleOnPlay);
        m_preferences.buildOnChange = preferences->value("buildOnChange", m_preferences.buildOnChange);
        m_preferences.backupMinutes = std::clamp(preferences->value("backupMinutes", m_preferences.backupMinutes), 0,
                                                 EditorPreferences::MaxBackupMinutes);
        m_preferences.externalEditor = preferences->value("externalEditor", m_preferences.externalEditor);
    }
}

bool EditorSettings::Save() const {
    if (m_file.empty()) {
        return true;
    }
    json recent = json::array();
    for (const RecentProject& project : m_recentProjects) {
        recent.push_back({
            {"file", PathToUtf8(project.file)},
            {"name", project.name},
            {"lastOpened",
             std::chrono::duration_cast<std::chrono::seconds>(project.lastOpened.time_since_epoch()).count()},
        });
    }
    const json gizmo = {
        {"tool", GizmoToolNames[static_cast<std::size_t>(m_gizmo.tool)]},
        {"local", m_gizmo.local},
        {"snap", m_gizmo.snap},
        {"moveStep", m_gizmo.moveStep},
        {"rotateStep", m_gizmo.rotateStep},
        {"scaleStep", m_gizmo.scaleStep},
    };
    const json preferences = {
        {"uiScale", m_preferences.uiScale},
        {"clearConsoleOnPlay", m_preferences.clearConsoleOnPlay},
        {"buildOnChange", m_preferences.buildOnChange},
        {"backupMinutes", m_preferences.backupMinutes},
        {"externalEditor", m_preferences.externalEditor},
    };
    const json document = {
        {"recentProjects", recent},
        {"projectsLocation", PathToUtf8(m_projectsLocation)},
        {"gizmo", gizmo},
        {"preferences", preferences},
    };

    std::error_code error;
    std::filesystem::create_directories(m_file.parent_path(), error);
    std::ofstream stream(m_file, std::ios::binary);
    stream << document.dump(2, ' ', false, json::error_handler_t::replace) << '\n';
    if (!stream) {
        GetEditorLogger().Error("Could not save the editor settings to '{}'", PathToUtf8(m_file));
        return false;
    }
    return true;
}

void EditorSettings::AddRecentProject(const std::filesystem::path& file, std::string name,
                                      std::chrono::system_clock::time_point opened) {
    RemoveRecentProject(file);
    m_recentProjects.insert(m_recentProjects.begin(), {.file = file, .name = std::move(name), .lastOpened = opened});
    if (m_recentProjects.size() > MaxRecentProjects) {
        m_recentProjects.resize(MaxRecentProjects);
    }
}

void EditorSettings::RemoveRecentProject(const std::filesystem::path& file) {
    std::erase_if(m_recentProjects, [&](const RecentProject& project) { return IsSameFile(project.file, file); });
}

std::filesystem::path GetDefaultProjectsLocation() {
    std::filesystem::path base = GetUserFolder(UserFolder::Documents);
    if (base.empty()) {
        base = GetUserFolder(UserFolder::Home);
    }
    return (base.empty() ? std::filesystem::current_path() : base) / "Spot Projects";
}

} // namespace Spot::Editor
