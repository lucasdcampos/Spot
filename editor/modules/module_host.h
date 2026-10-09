#pragma once

#include <spot.h>
#include <spot_tools.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace Spot::Editor {

// The game's code of a project with code, one with a CMakeLists.txt in its code folder: builds it in the background
// when it changes, and loads the module it builds, from a copy, so the next build can replace the file meanwhile.
class ModuleHost {
public:
    struct Options {
        Toolchain toolchain = Toolchain::GetBuilt();
        // How often the code is looked at for changes.
        std::chrono::milliseconds watchInterval = std::chrono::milliseconds(1000);
    };

    ModuleHost(const Project& project, Options options);
    // Unloads the module: no component of its types may be left, as their code goes with it.
    ~ModuleHost();

    ModuleHost(const ModuleHost&) = delete;
    ModuleHost& operator=(const ModuleHost&) = delete;

    bool HasCode() const;
    // Loads the module built last, so scenes open with the game's components, and builds the code when it changed
    // since. Returns whether a module loaded.
    bool LoadBuilt();
    // Builds the code in the background.
    void Build();
    // Follows the build, and builds when the code changed, unless told not to.
    void Update();
    // Whether changes to the code start a build; when they do not, HasUnbuiltChanges() tells of them.
    void SetBuildOnChange(bool build) { m_buildOnChange = build; }
    // Whether the code changed since the last build started.
    bool HasUnbuiltChanges() const { return m_unbuiltChanges; }

    // Whether a module was built since the one loaded, waiting for SwapModule().
    bool HasNewModule() const { return m_newModule; }
    // Unloads the module and loads the one built last. No component of the loaded module's types may be left.
    // Returns whether it loaded.
    bool SwapModule();

    const CodeBuild& GetBuild() const { return m_build; }
    // Null while none is loaded.
    const GameModule* GetModule() const { return m_module.get(); }

private:
    // Changes with any change to the code: its files, their sizes and times.
    std::uint64_t StampCode() const;
    std::filesystem::path GetShadowDirectory() const;

    std::filesystem::path m_directory;
    std::filesystem::path m_cacheDirectory;
    Options m_options;
    CodeBuild m_build;
    std::unique_ptr<GameModule> m_module;
    // The built module's time when it was loaded, to tell new builds from those that changed nothing.
    std::filesystem::file_time_type m_loadedTime;
    bool m_newModule = false;
    bool m_buildOnChange = true;
    bool m_unbuiltChanges = false;
    std::uint64_t m_codeStamp = 0;
    std::chrono::steady_clock::time_point m_lastWatch;
};

} // namespace Spot::Editor
