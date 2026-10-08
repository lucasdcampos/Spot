#include "player_application.h"

#include <spot.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <string_view>
#include <system_error>

namespace {

// A project's folder stands for its project file.
std::filesystem::path FindProjectFile(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) {
        return path;
    }
    for (const auto& entry : std::filesystem::directory_iterator(path, error)) {
        if (entry.path().extension() == Spot::Project::FileExtension) {
            return entry.path();
        }
    }
    return path;
}

// The game's code, loaded from a copy of its own, so the editor can build it again while the game runs.
class LoadedModule {
public:
    // Nothing to load is fine, as for projects without code; a module that cannot be loaded is not, and gives null.
    static std::unique_ptr<LoadedModule> Load(const Spot::Project& project) {
        auto loaded = std::unique_ptr<LoadedModule>(new LoadedModule());
        const std::filesystem::path built = project.GetModuleFile();
        std::error_code error;
        if (!std::filesystem::exists(built, error)) {
            return loaded;
        }
        const std::filesystem::path copy =
            project.GetCacheDirectory() / "player" /
            Spot::Utf8ToPath(
                std::format("game-{}{}", Spot::AssetId::Generate().ToString(), Spot::GameModule::FileExtension));
        std::filesystem::create_directories(copy.parent_path(), error);
        if (!std::filesystem::copy_file(built, copy, error)) {
            Spot::Player::GetPlayerLogger().Error("Could not copy the game's code to '{}': {}", Spot::PathToUtf8(copy),
                                                  error.message());
            return nullptr;
        }
        loaded->m_copy = copy;
        loaded->m_module = Spot::GameModule::Load(copy);
        return loaded->m_module ? std::move(loaded) : nullptr;
    }

    LoadedModule(const LoadedModule&) = delete;
    LoadedModule& operator=(const LoadedModule&) = delete;

    ~LoadedModule() {
        m_module.reset();
        if (!m_copy.empty()) {
            std::error_code error;
            std::filesystem::remove(m_copy, error);
        }
    }

private:
    LoadedModule() = default;

    std::unique_ptr<Spot::GameModule> m_module;
    std::filesystem::path m_copy;
};

} // namespace

// spot_player [--no-graphics] [--frames <count>] <project>
//   project          a project file, or a folder with one, to run from its startup scene with its game's code
//   --no-graphics    runs without a window's graphics, drawing nothing, as on machines without a GPU
//   --frames <count> quits after that many frames
int main(int argc, char** argv) {
    Spot::Player::PlayerOptions options;
    std::filesystem::path path;
    for (int i = 1; i < argc; ++i) {
        // Arguments come in the encoding the OS uses for narrow paths, which this constructor expects.
        const std::string_view argument = argv[i];
        if (argument == "--no-graphics") {
            options.graphics = false;
        } else if (argument == "--frames" && i + 1 < argc) {
            options.frames = std::max(1, std::atoi(argv[++i]));
        } else {
            path = std::filesystem::path(argv[i]);
        }
    }
    if (path.empty()) {
        Spot::Player::GetPlayerLogger().Error("Usage: spot_player [--no-graphics] [--frames <count>] <project>");
        return EXIT_FAILURE;
    }

    const auto project = Spot::Project::Load(FindProjectFile(path));
    if (!project) {
        return EXIT_FAILURE;
    }
    const auto module = LoadedModule::Load(*project);
    if (!module) {
        return EXIT_FAILURE;
    }
    // The game goes before its code.
    return Spot::Player::PlayerApplication(*project, options).Run();
}
