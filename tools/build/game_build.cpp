#include "build/game_build.h"

#include "tools_log.h"

#include "assets/asset_manifest.h"
#include "assets/asset_pak.h"

#include <spot_import.h>

#include <string>
#include <system_error>

namespace Spot {

namespace {

bool IsSharedLibrary(const std::filesystem::path& path) {
    const std::filesystem::path extension = path.extension();
    return extension == ".dll" || extension == ".so" || extension == ".dylib" ||
           path.filename().string().find(".so.") != std::string::npos;
}

bool ContainsPath(const std::filesystem::path& directory, std::filesystem::path path) {
    for (;;) {
        std::error_code error;
        if (path == directory || std::filesystem::equivalent(path, directory, error)) {
            return true;
        }
        const auto parent = path.parent_path();
        if (parent == path || parent.empty()) {
            return false;
        }
        path = parent;
    }
}

// The project's name as a file name: what file systems allow, which project names mostly are already.
std::string MakeFileName(std::string_view name) {
    std::string file;
    for (const char c : name) {
        const bool forbidden =
            static_cast<unsigned char>(c) < 32 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos;
        file += forbidden ? '_' : c;
    }
    return file.empty() ? "game" : file;
}

} // namespace

std::string_view GetPlatformName() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

std::size_t ImportProjectAssets(const Project& project) {
    std::error_code error;
    std::filesystem::create_directories(project.GetAssetsDirectory(), error);
    return AssetDatabase(project.GetAssetsDirectory(), project.GetImportDirectory()).Refresh();
}

bool CookGame(const Project& project, const std::filesystem::path& directory) {
    const GameSettings settings = project.GetGameSettings();
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        GetToolsLogger().Error("Could not make the folder '{}': {}", PathToUtf8(directory), error.message());
        return false;
    }
    AssetDatabase(project.GetAssetsDirectory(), directory / Utf8ToPath(settings.importDirectory)).Refresh();
    return settings.SaveToFile(directory / GameSettings::FileName);
}

bool WriteRunSettings(const Project& project) {
    const std::filesystem::path executable = project.GetGameExecutable();
    std::error_code error;
    if (!std::filesystem::is_regular_file(executable, error)) {
        GetToolsLogger().Error("The game of '{}' was not built: there is no '{}'", project.GetSettings().name,
                               PathToUtf8(executable));
        return false;
    }
    GameSettings settings = project.GetGameSettings();
    settings.importDirectory = PathToUtf8(project.GetImportDirectory());
    return settings.SaveToFile(executable.parent_path() / GameSettings::FileName);
}

std::filesystem::path GetExportDirectory(const Project& project) {
    return project.GetDirectory() / "builds" / Utf8ToPath(GetPlatformName());
}

bool ExportGame(const Project& project, const std::filesystem::path& directory) {
    const std::filesystem::path executable = project.GetGameExecutable();
    std::error_code error;
    const auto destination = std::filesystem::weakly_canonical(directory, error);
    if (directory.empty() || error || destination == destination.root_path() ||
        ContainsPath(destination, project.GetDirectory()) || ContainsPath(destination, executable) ||
        ContainsPath(destination, project.GetImportDirectory()) ||
        ContainsPath(project.GetAssetsDirectory(), destination) ||
        ContainsPath(project.GetCodeDirectory(), destination) ||
        ContainsPath(project.GetBuildDirectory(), destination)) {
        GetToolsLogger().Error("The export folder '{}' overlaps the project or its build inputs",
                               PathToUtf8(directory));
        return false;
    }
    if (!std::filesystem::is_regular_file(executable, error)) {
        GetToolsLogger().Error("The game of '{}' was not built: there is no '{}'", project.GetSettings().name,
                               PathToUtf8(executable));
        return false;
    }
    if (!project.GetSettings().startupScene.IsValid()) {
        GetToolsLogger().Error("'{}' has no startup scene for its game to start with", project.GetSettings().name);
        return false;
    }

    const auto fail = [&](std::string_view what) {
        GetToolsLogger().Error("Could not export the game to '{}': {} ({})", PathToUtf8(directory), what,
                               error.message());
        return false;
    };
    std::filesystem::remove_all(directory, error);
    if (error) {
        return fail("the previous export cannot be removed");
    }
    std::filesystem::create_directories(directory, error);
    if (error) {
        return fail("the folder cannot be made");
    }

    const std::filesystem::path name = Utf8ToPath(MakeFileName(project.GetSettings().name));
    std::filesystem::path shipped = directory / name;
    shipped += executable.extension();
    if (!std::filesystem::copy_file(executable, shipped, error)) {
        return fail("the executable cannot be copied");
    }
    // The engine library, and any other the game's code put beside its executable.
    for (const auto& entry : std::filesystem::directory_iterator(executable.parent_path(), error)) {
        if (entry.is_regular_file(error) && IsSharedLibrary(entry.path()) &&
            !std::filesystem::copy_file(entry.path(), directory / entry.path().filename(), error)) {
            return fail("a library cannot be copied");
        }
    }
    // The project's imports hold only the assets its sources have now, which is what the game ships with: their
    // manifest, and the assets themselves in one pak, which loads faster than a file per asset.
    const GameSettings settings = project.GetGameSettings();
    const std::filesystem::path shippedImports = directory / Utf8ToPath(settings.importDirectory);
    std::filesystem::create_directories(shippedImports, error);
    if (!error) {
        std::filesystem::copy_file(Detail::Manifest::GetPath(project.GetImportDirectory()),
                                   Detail::Manifest::GetPath(shippedImports), error);
    }
    if (error) {
        return fail("the imported assets' manifest cannot be copied");
    }
    if (!Detail::WriteAssetPak(shippedImports / Detail::AssetPakFileName, project.GetImportDirectory())) {
        return fail("the imported assets cannot be packed");
    }
    if (!settings.SaveToFile(directory / GameSettings::FileName)) {
        return false;
    }
    GetToolsLogger().Info("Exported '{}' to '{}'", project.GetSettings().name, PathToUtf8(directory));
    return true;
}

} // namespace Spot
