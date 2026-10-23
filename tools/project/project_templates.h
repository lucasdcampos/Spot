#pragma once

#include "project/project.h"

#include <spot.h>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Spot {

enum class ProjectTemplate {
    // A scene with a camera, a sun, a sky, a ground and a cube, ready to look around.
    Basic3D,
    // The same scene, with a component of the game's code that turns the cube.
    Code3D,
    // Only the project file and an empty assets folder.
    Empty,
};

// What a new project's game is made of besides its assets and scripts.
enum class ProjectCode {
    // Nothing: the game runs with the player the tools come with, so making it needs no compiler.
    None,
    // C++ code of its own, in its code directory, which the tools build with CMake.
    Cpp,
};

struct ProjectTemplateInfo {
    ProjectTemplate kind;
    const char* name;
    const char* description;
    // Whether the template is about the game's code, so its projects have C++ code.
    bool needsCode = false;
};

std::span<const ProjectTemplateInfo> GetProjectTemplates();

// Fills a new scene with what every scene starts with: a camera, a sun and a sky.
void PopulateNewScene(Scene& scene);

// Why a name cannot be a project's, which is also its folder's and its file's; empty when it can.
std::string ValidateProjectName(std::string_view name);

// Creates "<location>/<name>" from the template, which must not exist yet or be empty, and returns its project file.
// Returns null, after logging why, when it cannot, as for a template that needs code and a project without.
std::optional<std::filesystem::path> CreateProject(const std::filesystem::path& location, const std::string& name,
                                                   ProjectTemplate kind, ProjectCode code = ProjectCode::Cpp);
// Gives a project without code the C++ code every game with code starts with: the build script, the main() of its
// executable and the file marking its game module. From then on the project is built with CMake (see
// Project::HasCode()). Returns false, after logging why, when the files cannot be written.
bool AddProjectCode(const Project& project);
// Why the name cannot be a new component's, whose code would go in the folder of the project's code; empty when it
// can. Names are C++ class names that no component type has, nor the files of the folder.
std::string ValidateComponentName(const Project& project, std::string_view folder, std::string_view name);
// Writes "<name>.h" and "<name>.cpp" for a new component into the folder of the project's code, and, for projects
// without code, what building it takes (see AddProjectCode()). Returns their paths, relative to the code directory;
// null, after logging why, when it cannot.
std::optional<std::vector<std::string>> CreateComponentCode(const Project& project, std::string_view folder,
                                                            const std::string& name);

// Brings a project made by an older editor to the current layout (see Project): moves a CMakeLists.txt from the
// project's folder into its code folder, gives a project with code the main() of its executable, and has git ignore
// exported games. Projects without code keep none. Returns whether anything changed.
bool UpdateProjectLayout(const Project& project);

} // namespace Spot
