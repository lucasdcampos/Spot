#include "project/project_templates.h"

#include "tools_log.h"

#include <spot.h>
#include <spot_import.h>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <system_error>
#include <vector>

namespace Spot {

namespace {

constexpr std::array Templates{
    ProjectTemplateInfo{ProjectTemplate::Basic3D, "3D", "A lit scene with a sky, ready to fill."},
    ProjectTemplateInfo{ProjectTemplate::Code3D, "3D with a component",
                        "The same scene, with a component of the game's own C++ code that turns the cube, which the "
                        "editor builds and reloads as it changes.",
                        true},
    ProjectTemplateInfo{ProjectTemplate::Empty, "Empty", "An empty assets folder."},
};

// The generated folder is rebuilt from the sources, and exported games from the project, so neither is committed.
constexpr std::string_view GitIgnore = "# Imported assets and editor state, rebuilt from the sources\n/.spot/\n"
                                       "# Games exported from the project\n/builds/\n";

// How projects build their code; the tools tell CMake where the engine is.
constexpr std::string_view BuildScript =
    R"(# Builds the game's code into the game's executable, and into the game module that the editor loads. The tools
# build it, the editor whenever the code changes, pointing Spot_DIR at their own build of the engine.
cmake_minimum_required(VERSION 3.25)
project({} LANGUAGES CXX)

find_package(Spot CONFIG REQUIRED)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS *.cpp *.h)
spot_add_game(${{sources}})
)";

// What the build script of projects made before games had their own executable called.
constexpr std::string_view OldModuleCall = "spot_add_game_module(game ${sources})";
constexpr std::string_view GameCall = "spot_add_game(${sources})";

constexpr std::string_view MainCode = R"(#include <spot.h>

// Runs the game from its startup scene, as the settings the tools write beside the executable say.
int main() {
    Spot::Application application({.game = Spot::GameSettings::LoadBesideExecutable()});
    application.AddModule<Spot::UiModule>();
    application.AddModule<Spot::RenderModule>();
    application.AddModule<Spot::AudioModule>();
    application.AddModule<Spot::PhysicsModule>();
    application.AddModule<Spot::Physics2DModule>();
    application.AddModule<Spot::AnimationModule>();
    application.AddModule<Spot::ParticlesModule>();
    application.AddModule<Spot::ScriptingModule>();
    // The game's own code, which SPOT_GAME_MODULE() names.
    application.AddModule(Spot::CreateGameModule());
    return application.Run();
}
)";

constexpr std::string_view GameCode = R"(#include <spot.h>

namespace {

// Turns its entity around the vertical axis, faster one way or the other with the Move action. The game's own
// components are made like the engine's, and show in the editor's inspector beside them once the editor has built the
// code.
class Rotator final : public Spot::Component {
public:
    // Radians per second; the inspector shows degrees.
    float speed = 1.0f;

protected:
    void OnUpdate(float deltaTime) override {
        // Actions come from the project's input settings, whatever controls play them: A and D, or the left stick.
        const float turn = speed * (1.0f + 2.0f * Spot::Input::GetActionVector2("Move").x);
        Spot::Transform& transform = GetEntity().GetTransform();
        transform.SetRotation(Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, turn * deltaTime) *
                              transform.GetRotation());
    }

    // What scenes save, and the inspector shows.
    void Serialize(Spot::Archive& archive) override {
        archive.Field("speed", speed, {.angle = true, .tooltip = "How fast it turns, per second"});
    }
};

} // namespace

// Registers the component when the game's module starts: when the game runs, and when the editor loads its code.
SPOT_COMPONENT(Rotator);

// Marks the game's code, whose module the game's executable runs and the editor loads.
SPOT_GAME_MODULE()
)";

// The code every project starts with, which components are added to.
constexpr std::string_view ModuleCode = R"(#include <spot.h>

// Marks the game's code, whose module the game's executable runs and the editor loads. Components marked with
// SPOT_COMPONENT register themselves when it starts; a game with more to do names a module of its own deriving from
// Spot::GameCodeModule, as in SPOT_GAME_MODULE(Game).
SPOT_GAME_MODULE()
)";

constexpr std::string_view ComponentHeader = R"(#pragma once

#include <spot.h>

class {0} final : public Spot::Component {{
protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
}};
)";

constexpr std::string_view ComponentSource = R"(#include "{0}.h"

SPOT_COMPONENT({0});

void {0}::OnStart() {{
    
}}

void {0}::OnUpdate(float deltaTime) {{
    
}}
)";

// Words C++ keeps for itself, which cannot name a class.
constexpr std::array Keywords{
    "alignas",
    "alignof",
    "and",
    "asm",
    "auto",
    "bool",
    "break",
    "case",
    "catch",
    "char",
    "class",
    "concept",
    "const",
    "consteval",
    "constexpr",
    "constinit",
    "const_cast",
    "continue",
    "co_await",
    "co_return",
    "co_yield",
    "decltype",
    "default",
    "delete",
    "do",
    "double",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "float",
    "for",
    "friend",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "not",
    "nullptr",
    "operator",
    "or",
    "private",
    "protected",
    "public",
    "register",
    "reinterpret_cast",
    "requires",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "static_assert",
    "static_cast",
    "struct",
    "switch",
    "template",
    "this",
    "throw",
    "true",
    "try",
    "typedef",
    "typeid",
    "typename",
    "union",
    "unsigned",
    "using",
    "virtual",
    "void",
    "volatile",
    "while",
    "xor",
};

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

// Writes the build script, main.cpp and the file with SPOT_GAME_MODULE() into the folder of the project's code, each
// unless it is there already, as for projects made by older editors.
bool WriteCodeFiles(const Project& project, std::string_view gameCode) {
    const std::filesystem::path code = project.GetCodeDirectory();
    const std::string identifier = MakeIdentifier(project.GetSettings().name);
    std::error_code error;
    const auto write = [&](const char* name, std::string_view text) {
        return std::filesystem::exists(code / name, error) || WriteText(code / name, text);
    };
    return write("CMakeLists.txt", std::vformat(BuildScript, std::make_format_args(identifier))) &&
           write("main.cpp", MainCode) && write("game.cpp", gameCode);
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

std::string ValidateComponentName(const Project& project, std::string_view folder, std::string_view name) {
    if (name.empty()) {
        return "Enter a name for the component.";
    }
    const auto isLetter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (!isLetter(name.front()) ||
        !std::ranges::all_of(name, [&](char c) { return isLetter(c) || (c >= '0' && c <= '9'); })) {
        return "Names are C++ class names: letters, digits and '_', not starting with a digit.";
    }
    if (std::ranges::find(Keywords, name) != Keywords.end()) {
        return "C++ keeps that word for itself.";
    }
    // Scenes save components by name, so two types cannot share one.
    const std::vector<std::string> types = GetComponentTypeNames();
    if (std::ranges::find(types, name) != types.end()) {
        return "A component type already has that name.";
    }
    const std::filesystem::path directory = project.GetCodeDirectory() / Utf8ToPath(folder);
    std::error_code error;
    for (const char* extension : {".h", ".cpp"}) {
        if (std::filesystem::exists(directory / Utf8ToPath(std::string(name) + extension), error)) {
            return std::format("The folder already has {}{}.", name, extension);
        }
    }
    return {};
}

std::optional<std::vector<std::string>> CreateComponentCode(const Project& project, std::string_view folder,
                                                            const std::string& name) {
    if (const std::string problem = ValidateComponentName(project, folder, name); !problem.empty()) {
        GetToolsLogger().Error("Cannot create the component '{}': {}", name, problem);
        return std::nullopt;
    }
    // Projects without code get what building it takes first.
    if (!AddProjectCode(project)) {
        return std::nullopt;
    }
    const std::filesystem::path code = project.GetCodeDirectory();
    const std::string header = folder.empty() ? name + ".h" : std::string(folder) + "/" + name + ".h";
    const std::string source = folder.empty() ? name + ".cpp" : std::string(folder) + "/" + name + ".cpp";
    if (!WriteText(code / Utf8ToPath(header), std::vformat(ComponentHeader, std::make_format_args(name))) ||
        !WriteText(code / Utf8ToPath(source), std::vformat(ComponentSource, std::make_format_args(name)))) {
        GetToolsLogger().Error("Could not write the code of the component '{}'", name);
        return std::nullopt;
    }
    GetToolsLogger().Info("Created the component '{}'; it can be added to entities once the code is built", name);
    return std::vector{header, source};
}

bool AddProjectCode(const Project& project) {
    const bool hadCode = project.HasCode();
    if (!WriteCodeFiles(project, ModuleCode)) {
        GetToolsLogger().Error("Could not write the project's code in '{}'", PathToUtf8(project.GetCodeDirectory()));
        return false;
    }
    if (!hadCode) {
        GetToolsLogger().Info("The project has C++ code now, in 'src', which the tools build with CMake");
    }
    return true;
}

bool UpdateProjectLayout(const Project& project) {
    const std::filesystem::path directory = project.GetDirectory();
    const std::filesystem::path oldScript = directory / "CMakeLists.txt";
    const std::filesystem::path newScript = project.GetCodeDirectory() / "CMakeLists.txt";
    std::error_code error;
    bool changed = false;

    // The build script went into the code folder, where the paths of the code are relative to it.
    if (std::filesystem::is_regular_file(oldScript, error) && !std::filesystem::exists(newScript, error)) {
        std::string text;
        {
            std::ifstream file(oldScript, std::ios::binary);
            text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
        for (const std::string_view prefix : {"${CMAKE_CURRENT_SOURCE_DIR}/src/", "src/"}) {
            for (std::size_t at = text.find(prefix); at != std::string::npos; at = text.find(prefix, at)) {
                text.erase(at, prefix.size());
            }
        }
        if (!WriteText(newScript, text) || !std::filesystem::remove(oldScript, error)) {
            GetToolsLogger().Error("Could not move '{}' into '{}'", PathToUtf8(oldScript),
                                   PathToUtf8(project.GetCodeDirectory()));
            return false;
        }
        GetToolsLogger().Info("Moved the project's CMakeLists.txt into its code folder, 'src'");
        changed = true;
    }

    // Games have an executable of their own, which projects made before had no code or build for.
    std::string script;
    {
        std::ifstream file(newScript, std::ios::binary);
        script.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    if (const std::size_t at = script.find(OldModuleCall); at != std::string::npos) {
        script.replace(at, OldModuleCall.size(), GameCall);
        if (!WriteText(newScript, script)) {
            GetToolsLogger().Error("Could not update '{}'", PathToUtf8(newScript));
            return false;
        }
        GetToolsLogger().Info("The project's code builds the game's executable now, with spot_add_game()");
        changed = true;
    }
    // Code built before games had their own executable has no main() for it.
    if (std::filesystem::exists(newScript, error) &&
        !std::filesystem::exists(project.GetCodeDirectory() / "main.cpp", error)) {
        if (!WriteCodeFiles(project, ModuleCode)) {
            GetToolsLogger().Error("Could not write the project's code in '{}'",
                                   PathToUtf8(project.GetCodeDirectory()));
            return false;
        }
        GetToolsLogger().Info("Added the game's main.cpp to the project's code");
        changed = true;
    }

    const std::filesystem::path ignore = directory / ".gitignore";
    if (std::filesystem::is_regular_file(ignore, error)) {
        std::ifstream file(ignore, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        file.close();
        if (text.find("/builds/") == std::string::npos) {
            std::ofstream(ignore, std::ios::binary | std::ios::app)
                << (text.empty() || text.ends_with('\n') ? "" : "\n")
                << "# Games exported from the project\n/builds/\n";
            changed = true;
        }
    }
    return changed;
}

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
                                                   ProjectTemplate kind, ProjectCode code) {
    if (const std::string problem = ValidateProjectName(name); !problem.empty()) {
        GetToolsLogger().Error("Cannot create project '{}': {}", name, problem);
        return std::nullopt;
    }
    const auto info = std::ranges::find(Templates, kind, &ProjectTemplateInfo::kind);
    if (info->needsCode && code == ProjectCode::None) {
        GetToolsLogger().Error("Cannot create project '{}': the template '{}' needs C++ code", name, info->name);
        return std::nullopt;
    }
    const std::filesystem::path directory = location / Utf8ToPath(name);
    std::error_code error;
    if (std::filesystem::exists(directory, error) && !std::filesystem::is_empty(directory, error)) {
        GetToolsLogger().Error("Cannot create project '{}': '{}' is not empty", name, PathToUtf8(directory));
        return std::nullopt;
    }

    auto project = Project::Create(
        directory,
        {.name = name, .sections = {.input = InputSettings::MakeDefault(), .audio = AudioSettings::MakeDefault()}});
    if (!project) {
        return std::nullopt;
    }
    std::ofstream(directory / ".gitignore", std::ios::binary) << GitIgnore;

    if (code == ProjectCode::Cpp &&
        !WriteCodeFiles(*project, kind == ProjectTemplate::Code3D ? GameCode : ModuleCode)) {
        GetToolsLogger().Error("Could not write the code of project '{}'", name);
        return std::nullopt;
    }
    if (kind == ProjectTemplate::Basic3D || kind == ProjectTemplate::Code3D) {
        Scene scene;
        BuildBasic3DScene(scene);
        if (kind == ProjectTemplate::Code3D) {
            AddComponentFromString(*scene.FindEntity("Cube"), CubeRotator);
        }
        const auto sceneId = WriteScene(scene, project->GetAssetsDirectory() / "scenes" / "Main.scene");
        if (!sceneId) {
            GetToolsLogger().Error("Could not write the scene of project '{}'", name);
            return std::nullopt;
        }
        project->GetSettings().startupScene = *sceneId;
        if (!project->Save()) {
            return std::nullopt;
        }
    }
    GetToolsLogger().Info("Created project '{}' in '{}'", name, PathToUtf8(directory));
    return project->GetFile();
}

} // namespace Spot
