#include "project/project_templates.h"

#include "core/editor_log.h"

#include <spot.h>

#include <array>
#include <format>
#include <fstream>
#include <system_error>

namespace Spot::Editor {

namespace {

constexpr std::array Templates{
    ProjectTemplateInfo{ProjectTemplate::Basic3D, "3D", "A lit scene with a sky, ready to fill."},
    ProjectTemplateInfo{ProjectTemplate::Code3D, "3D with C++",
                        "The same scene, and C++ code for the game's own components, which the editor builds and "
                        "reloads as it changes."},
    ProjectTemplateInfo{ProjectTemplate::Empty, "Empty", "Only an empty assets folder."},
};

// The generated folder is rebuilt from the sources, so it is never committed.
constexpr std::string_view GitIgnore = "# Imported assets and editor state, rebuilt from the sources\n/.spot/\n";

// How projects with code build it; the editor tells CMake where the engine is.
constexpr std::string_view BuildScript =
    R"(# Builds the game's code into the game module that the editor and the player load. The editor builds it whenever
# the code changes, pointing Spot_DIR at its own build of the engine.
cmake_minimum_required(VERSION 3.25)
project({} LANGUAGES CXX)

find_package(Spot CONFIG REQUIRED)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS src/*.cpp src/*.h)
spot_add_game_module(game ${{sources}})
)";

constexpr std::string_view GameCode = R"(#include <spot.h>

namespace {

// Turns its entity around the vertical axis. The game's own components are made like the engine's, and show in the
// editor's inspector beside them once the editor has built the code.
class Rotator final : public Spot::Component {
public:
    // Radians per second; the inspector shows degrees.
    float speed = 1.0f;

protected:
    void OnUpdate(float deltaTime) override {
        Spot::Transform& transform = GetEntity().GetTransform();
        transform.SetRotation(Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, speed * deltaTime) *
                              transform.GetRotation());
    }

    // What scenes save, and the inspector shows.
    void Serialize(Spot::Archive& archive) override {
        archive.Field("speed", speed, {.angle = true, .tooltip = "How fast it turns, per second"});
    }
};

// Runs when the editor or the player loads the game's code.
void RegisterGame() {
    Spot::RegisterComponent<Rotator>("Rotator");
}

} // namespace

SPOT_GAME_MODULE(RegisterGame)
)";

// The game's code turns the cube. Until the code is built, the cube keeps its component as it was saved.
constexpr std::string_view CubeRotator = R"({"type":"Rotator","enabled":true,"data":{"speed":0.5}})";

// CMake project names are identifiers.
std::string MakeIdentifier(std::string_view name) {
    std::string identifier;
    for (const char c : name) {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        identifier += valid ? c : '_';
    }
    if (identifier.empty() || (identifier.front() >= '0' && identifier.front() <= '9')) {
        identifier.insert(identifier.begin(), '_');
    }
    return identifier;
}

bool WriteText(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary);
    file << text;
    return static_cast<bool>(file);
}

void BuildBasic3DScene(Scene& scene) {
    PopulateNewScene(scene);

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

void PopulateNewScene(Scene& scene) {
    // What the game shows of the scene, looking at the middle of it from a little above.
    Entity& camera = scene.CreateEntity("Camera");
    camera.AddComponent<Camera>();
    const Vector3 eye{0.0f, 2.0f, 6.0f};
    camera.GetTransform().SetPosition(eye);
    camera.GetTransform().SetRotation(Quaternion::LookRotation(Vector3{0.0f, 0.5f, 0.0f} - eye));

    Entity& sun = scene.CreateEntity("Sun");
    sun.AddComponent<DirectionalLight>();
    sun.GetTransform().SetRotation(Quaternion::LookRotation({-0.4f, -1.0f, -0.6f}));

    scene.CreateEntity("Sky").AddComponent<Sky>();
}

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

    if (kind == ProjectTemplate::Code3D) {
        const std::string identifier = MakeIdentifier(name);
        if (!WriteText(directory / "CMakeLists.txt", std::vformat(BuildScript, std::make_format_args(identifier))) ||
            !WriteText(directory / "src" / "game.cpp", GameCode)) {
            GetEditorLogger().Error("Could not write the code of project '{}'", name);
            return std::nullopt;
        }
    }
    if (kind == ProjectTemplate::Basic3D || kind == ProjectTemplate::Code3D) {
        Scene scene;
        BuildBasic3DScene(scene);
        if (kind == ProjectTemplate::Code3D) {
            AddComponentFromString(*scene.FindEntity("Cube"), CubeRotator);
        }
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
