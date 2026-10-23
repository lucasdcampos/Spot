// spot: the command line of the tools, which does what the editor does with projects, for scripts and builds that
// run without it.
#include <spot.h>
#include <spot_tools.h>

#include "assets/asset_pak.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr std::string_view Usage = R"(Usage: spot <command> [options]

Commands:
  new <name> [--path <folder>] [--template 3d|component|empty] [--code cpp|none]
                     Creates a project in a new folder named after it, in the folder or the current one; with
                     --code none, it has no C++ code and runs with the player, so it needs no compiler
  cook [<project>] [--out <folder>]
                     Imports the project's assets that changed, and writes the engine's API for Luau's language
                     server into .spot/luau; with --out, into that folder, with the game's settings, so a game's
                     executable there runs the game
  build [<project>]  Builds the game's code, if it has any: its executable and the module the editor loads
  run [<project>]    Builds the game, imports its assets and runs it
  export [<project>] [--out <folder>]
                     Builds the game and makes the folder it ships in, builds/<platform> by default
  pack <imported folder>
                     Packs the imported assets of the folder into its assets.pak, as export does, which the
                     engine then loads them from
  help               Shows this

A project is its .spot file or the folder that has one; the current folder by default.
)";

struct Arguments {
    std::vector<std::string> positionals;
    std::vector<std::pair<std::string, std::string>> options;

    std::optional<std::string> Get(std::string_view name) const {
        const auto found = std::ranges::find_if(options, [&](const auto& option) { return option.first == name; });
        return found == options.end() ? std::nullopt : std::optional(found->second);
    }
};

// Options take the argument after them: "--out builds".
std::optional<Arguments> ParseArguments(int argc, char** argv, int first) {
    Arguments arguments;
    for (int i = first; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument.starts_with("--")) {
            if (i + 1 >= argc) {
                Spot::GetToolsLogger().Error("'{}' needs a value after it", argument);
                return std::nullopt;
            }
            arguments.options.emplace_back(std::string(argument.substr(2)), argv[++i]);
        } else {
            arguments.positionals.emplace_back(argument);
        }
    }
    return arguments;
}

// Arguments come in the encoding the OS uses for narrow paths, which this constructor expects.
std::filesystem::path ToPath(const std::string& argument) {
    return std::filesystem::path(argument);
}

// A project's folder stands for its project file.
std::optional<Spot::Project> LoadProject(const Arguments& arguments) {
    std::filesystem::path path =
        arguments.positionals.empty() ? std::filesystem::current_path() : ToPath(arguments.positionals.front());
    std::error_code error;
    if (std::filesystem::is_directory(path, error)) {
        const std::filesystem::path folder = path;
        for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
            if (entry.path().extension() == Spot::Project::FileExtension) {
                path = entry.path();
                break;
            }
        }
        if (path == folder) {
            Spot::GetToolsLogger().Error("'{}' has no project file", Spot::PathToUtf8(folder));
            return std::nullopt;
        }
    }
    auto project = Spot::Project::Load(path);
    if (project) {
        // As when the editor opens it.
        Spot::UpdateProjectLayout(*project);
    }
    return project;
}

// Games without code run with the player, which has nothing to build.
bool BuildCode(const Spot::Project& project) {
    if (!project.HasCode()) {
        return true;
    }
    Spot::CodeBuild build(project.GetCodeDirectory(), project.GetBuildDirectory(), Spot::Toolchain::GetBuilt());
    build.Start();
    build.Wait();
    return build.GetState() == Spot::CodeBuildState::Succeeded;
}

int New(const Arguments& arguments) {
    if (arguments.positionals.size() != 1) {
        Spot::GetToolsLogger().Error("'new' takes the project's name: spot new <name>");
        return EXIT_FAILURE;
    }
    const std::string kind = arguments.Get("template").value_or("3d");
    Spot::ProjectTemplate projectTemplate = Spot::ProjectTemplate::Basic3D;
    if (kind == "component") {
        projectTemplate = Spot::ProjectTemplate::Code3D;
    } else if (kind == "empty") {
        projectTemplate = Spot::ProjectTemplate::Empty;
    } else if (kind != "3d") {
        Spot::GetToolsLogger().Error("There is no template '{}'; there are 3d, component and empty", kind);
        return EXIT_FAILURE;
    }
    const std::string code = arguments.Get("code").value_or("cpp");
    if (code != "cpp" && code != "none") {
        Spot::GetToolsLogger().Error("Projects have C++ code or none: --code cpp or --code none, not '{}'", code);
        return EXIT_FAILURE;
    }
    const auto location = arguments.Get("path");
    const auto file = Spot::CreateProject(location ? ToPath(*location) : std::filesystem::current_path(),
                                          arguments.positionals.front(), projectTemplate,
                                          code == "cpp" ? Spot::ProjectCode::Cpp : Spot::ProjectCode::None);
    return file ? EXIT_SUCCESS : EXIT_FAILURE;
}

int Cook(const Arguments& arguments) {
    const auto project = LoadProject(arguments);
    if (!project) {
        return EXIT_FAILURE;
    }
    // Editors' Luau language servers check scripts against the API of the engine that cooks them.
    Spot::WriteLuauTypes(*project);
    if (const auto out = arguments.Get("out")) {
        return Spot::CookGame(*project, ToPath(*out)) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    const std::size_t imported = Spot::ImportProjectAssets(*project);
    Spot::GetToolsLogger().Info("Imported {} asset{} of '{}'", imported, imported == 1 ? "" : "s",
                                project->GetSettings().name);
    return EXIT_SUCCESS;
}

int Build(const Arguments& arguments) {
    const auto project = LoadProject(arguments);
    return project && BuildCode(*project) ? EXIT_SUCCESS : EXIT_FAILURE;
}

int Run(const Arguments& arguments) {
    const auto project = LoadProject(arguments);
    if (!project || !BuildCode(*project)) {
        return EXIT_FAILURE;
    }
    Spot::ImportProjectAssets(*project);
    const auto launch = Spot::PrepareGameRun(*project);
    if (!launch) {
        return EXIT_FAILURE;
    }
    Spot::GameRunner game;
    if (!game.Start(launch->executable, launch->arguments)) {
        return EXIT_FAILURE;
    }
    while (game.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        game.Update();
    }
    game.Update();
    return game.GetExitCode().value_or(EXIT_FAILURE);
}

int Export(const Arguments& arguments) {
    const auto project = LoadProject(arguments);
    if (!project || !BuildCode(*project)) {
        return EXIT_FAILURE;
    }
    Spot::ImportProjectAssets(*project);
    const auto out = arguments.Get("out");
    return Spot::ExportGame(*project, out ? ToPath(*out) : Spot::GetExportDirectory(*project)) ? EXIT_SUCCESS
                                                                                               : EXIT_FAILURE;
}

int Pack(const Arguments& arguments) {
    if (arguments.positionals.size() != 1) {
        Spot::GetToolsLogger().Error("'spot pack' takes the folder of imported assets");
        return EXIT_FAILURE;
    }
    const std::filesystem::path folder = ToPath(arguments.positionals[0]);
    if (!Spot::Detail::WriteAssetPak(folder / Spot::Detail::AssetPakFileName, folder)) {
        return EXIT_FAILURE;
    }
    Spot::GetToolsLogger().Info("Packed the assets of '{}'", Spot::PathToUtf8(folder));
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    const std::string_view command = argc > 1 ? argv[1] : "help";
    if (command == "help" || command == "--help" || command == "-h") {
        std::cout << Usage;
        return EXIT_SUCCESS;
    }
    const auto arguments = ParseArguments(argc, argv, 2);
    if (!arguments) {
        return EXIT_FAILURE;
    }
    if (command == "new") {
        return New(*arguments);
    }
    if (command == "cook") {
        return Cook(*arguments);
    }
    if (command == "build") {
        return Build(*arguments);
    }
    if (command == "run") {
        return Run(*arguments);
    }
    if (command == "export") {
        return Export(*arguments);
    }
    if (command == "pack") {
        return Pack(*arguments);
    }
    Spot::GetToolsLogger().Error("There is no command '{}'; 'spot help' lists them", command);
    return EXIT_FAILURE;
}
