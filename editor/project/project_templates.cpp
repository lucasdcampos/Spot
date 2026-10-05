#include "project/project_templates.h"

#include "core/editor_log.h"

#include <spot.h>

#include <array>
#include <fstream>
#include <system_error>

namespace Spot::Editor {

namespace {

constexpr std::array Templates{
    ProjectTemplateInfo{ProjectTemplate::Basic3D, "3D", "A lit scene with a sky, ready to fill."},
    ProjectTemplateInfo{ProjectTemplate::Empty, "Empty", "Only an empty assets folder."},
};

// The generated folder is rebuilt from the sources, so it is never committed.
constexpr std::string_view GitIgnore = "# Imported assets and editor state, rebuilt from the sources\n/.spot/\n";

void BuildBasic3DScene(Scene& scene) {
    Entity& sun = scene.CreateEntity("Sun");
    sun.AddComponent<DirectionalLight>();
    sun.GetTransform().SetRotation(Quaternion::LookRotation({-0.4f, -1.0f, -0.6f}));

    scene.CreateEntity("Sky").AddComponent<Sky>();

    Entity& ground = scene.CreateEntity("Ground");
    ground.AddComponent<MeshRenderer>(AssetRef<Mesh>(BuiltInAssets::PlaneMesh, nullptr));
    ground.GetTransform().SetScale({20.0f, 1.0f, 20.0f});

    Entity& cube = scene.CreateEntity("Cube");
    cube.AddComponent<MeshRenderer>(AssetRef<Mesh>(BuiltInAssets::CubeMesh, nullptr));
    cube.GetTransform().SetPosition({0.0f, 0.5f, 0.0f});
}

// Writes the scene with its meta file, so its id is known before the first import.
std::optional<AssetId> WriteScene(const Scene& scene, const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error || !scene.SaveToFile(path)) {
        return std::nullopt;
    }
    AssetMeta meta;
    meta.id = AssetId::Generate();
    meta.type = AssetType::Scene;
    if (!meta.SaveToFile(AssetMeta::GetMetaPath(path))) {
        return std::nullopt;
    }
    return meta.id;
}

} // namespace

std::span<const ProjectTemplateInfo> GetProjectTemplates() {
    return Templates;
}

std::string ValidateProjectName(std::string_view name) {
    if (name.empty()) {
        return "Enter a name for the project.";
    }
    // Names become folder and file names, which some file systems restrict.
    if (name.find_first_of("<>:\"/\\|?*") != std::string_view::npos) {
        return "Names cannot contain < > : \" / \\ | ? or *.";
    }
    if (name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        return "Names cannot start or end with a space, or end with a period.";
    }
    for (const char c : name) {
        if (static_cast<unsigned char>(c) < 32) {
            return "Names cannot contain control characters.";
        }
    }
    return {};
}

std::optional<std::filesystem::path> CreateProject(const std::filesystem::path& location, const std::string& name,
                                                   ProjectTemplate kind) {
    if (const std::string problem = ValidateProjectName(name); !problem.empty()) {
        GetEditorLogger().Error("Cannot create project '{}': {}", name, problem);
        return std::nullopt;
    }
    const std::filesystem::path directory = location / Utf8ToPath(name);
    std::error_code error;
    if (std::filesystem::exists(directory, error) && !std::filesystem::is_empty(directory, error)) {
        GetEditorLogger().Error("Cannot create project '{}': '{}' is not empty", name, PathToUtf8(directory));
        return std::nullopt;
    }

    auto project = Project::Create(directory, {.name = name});
    if (!project) {
        return std::nullopt;
    }
    std::ofstream(directory / ".gitignore", std::ios::binary) << GitIgnore;

    if (kind == ProjectTemplate::Basic3D) {
        Scene scene;
        BuildBasic3DScene(scene);
        const auto sceneId = WriteScene(scene, project->GetAssetsDirectory() / "scenes" / "Main.scene");
        if (!sceneId) {
            GetEditorLogger().Error("Could not write the scene of project '{}'", name);
            return std::nullopt;
        }
        project->GetSettings().startupScene = *sceneId;
        if (!project->Save()) {
            return std::nullopt;
        }
    }
    GetEditorLogger().Info("Created project '{}' in '{}'", name, PathToUtf8(directory));
    return project->GetFile();
}

} // namespace Spot::Editor
