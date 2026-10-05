#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Spot::Editor {

enum class ProjectTemplate {
    // A scene with a sun, a sky, a ground and a cube, ready to look around.
    Basic3D,
    // Only the project file and an empty assets folder.
    Empty,
};

struct ProjectTemplateInfo {
    ProjectTemplate kind;
    const char* name;
    const char* description;
};

std::span<const ProjectTemplateInfo> GetProjectTemplates();

// Why a name cannot be a project's, which is also its folder's and its file's; empty when it can.
std::string ValidateProjectName(std::string_view name);

// Creates "<location>/<name>" from the template, which must not exist yet or be empty, and returns its project file.
// Returns null, after logging why, when it cannot.
std::optional<std::filesystem::path> CreateProject(const std::filesystem::path& location, const std::string& name,
                                                   ProjectTemplate kind);

} // namespace Spot::Editor
