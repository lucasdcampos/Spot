#pragma once

#include <spot.h>

#include <chrono>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Spot::Editor {

// How game modules are built so they work with this editor: with the compiler, generator and configuration that
// built it, against its own build of the shared engine library.
struct ModuleToolchain {
    std::string cmake;
    std::string generator;
    // The generator's platform, toolset and instance, as Visual Studio's have; empty for others.
    std::string platform;
    std::string toolset;
    std::string instance;
    // The compiler and make program, which only generators without their own pick of them use.
    std::string compiler;
    std::string makeProgram;
    std::string configuration;
    bool multiConfiguration = false;
    // Where the engine's CMake package is: Spot_DIR.
    std::filesystem::path package;

    // The toolchain that built the editor.
    static ModuleToolchain GetEditor();
    // Changes with anything that needs a project's build directory made again.
    std::string GetKey() const;
};

enum class ModuleBuildState {
    Idle,
    Running,
    Succeeded,
    Failed,
};

// Builds a project's code into its game module, with CMake, in the background: its output goes to the console as
// "Build", with the errors and warnings it recognizes as such.
class ModuleBuild {
public:
    // The build directory is the project's generated one; the module lands in its "module" folder.
    ModuleBuild(std::filesystem::path sourceDirectory, std::filesystem::path buildDirectory, ModuleToolchain toolchain);
    ~ModuleBuild();

    ModuleBuild(const ModuleBuild&) = delete;
    ModuleBuild& operator=(const ModuleBuild&) = delete;

    // Starts a build, configuring the build directory first when it was not yet, or for another toolchain. A build
    // running already starts over once it ends.
    void Start();
    // Follows the running build. Returns true the call it ends.
    bool Update();
    // Waits for the running build to end, following it.
    void Wait();

    // Whether the build directory was configured for this toolchain and these sources, so what it built works with
    // this editor.
    bool IsConfigured() const;

    ModuleBuildState GetState() const { return m_state; }
    bool IsRunning() const { return m_state == ModuleBuildState::Running; }
    std::size_t GetErrorCount() const { return m_errors; }
    std::size_t GetWarningCount() const { return m_warnings; }
    // How long the last build took, or the running one has taken so far.
    std::chrono::duration<float> GetDuration() const;
    // The module the build makes.
    std::filesystem::path GetModulePath() const;

    // Whether the line of compiler or CMake output reports an error or a warning, and how bad.
    static LogLevel ClassifyLine(std::string_view line);

private:
    enum class Step {
        Configure,
        Build,
    };

    void StartStep(Step step);
    void Log(const std::vector<std::string>& lines);
    void FlushMessage();
    void Finish(bool succeeded);
    std::string GetConfigurationKey() const;

    std::filesystem::path m_source;
    std::filesystem::path m_build;
    ModuleToolchain m_toolchain;
    std::unique_ptr<Process> m_process;
    Step m_step = Step::Build;
    ModuleBuildState m_state = ModuleBuildState::Idle;
    bool m_restart = false;
    std::size_t m_errors = 0;
    std::size_t m_warnings = 0;
    std::chrono::steady_clock::time_point m_started;
    std::chrono::steady_clock::time_point m_ended;
    // Errors and warnings seen in this build, as builds that report them again at their end would show them twice.
    std::vector<std::string> m_reported;
    // CMake's messages span several lines: the first one and those indented below it.
    LogLevel m_messageLevel = LogLevel::Trace;
    std::string m_message;
    // The last lines that were neither errors nor warnings.
    std::deque<std::string> m_tail;
};

} // namespace Spot::Editor
