#pragma once

#include <spot.h>

#include <chrono>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Spot {

// How a game's code is built so it works with these tools, and the editor built with them: with the compiler,
// generator and configuration that built them, against their own build of the shared engine library.
struct Toolchain {
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
    // The shared engine library games link, which the editor runs with. Modules built before it last changed may
    // not match it, and are built again before they load.
    std::filesystem::path engineLibrary;
    // The player, which runs games without code of their own (see Project::HasCode()), with the engine library
    // beside it.
    std::filesystem::path player;

    // The toolchain that built the tools.
    static Toolchain GetBuilt();
    // Changes with anything that needs a project's build directory made again.
    std::string GetKey() const;
    // Whether its CMake, and its compiler for generators that need one named, are still where they were, so it can
    // build games' code.
    bool IsAvailable() const;
};

// How long each phase of a build took. Compiling and linking are told apart when the build tool says when it links,
// as Ninja, Make and the MSVC linker do; otherwise compile holds both. Link holds whatever the build does after it
// starts linking, such as building the game's executable.
struct CodeBuildPhases {
    std::chrono::duration<float> configure = {};
    std::chrono::duration<float> compile = {};
    std::chrono::duration<float> link = {};
    bool linkMeasured = false;
};

enum class CodeBuildState {
    Idle,
    Running,
    Succeeded,
    Failed,
};

// Builds a project's code, with CMake, in the background: its output is logged as "Build", with the errors and
// warnings it recognizes as such.
class CodeBuild {
public:
    // The build directory is the project's generated one; the module lands in its "module" folder.
    CodeBuild(std::filesystem::path sourceDirectory, std::filesystem::path buildDirectory, Toolchain toolchain);
    ~CodeBuild();

    CodeBuild(const CodeBuild&) = delete;
    CodeBuild& operator=(const CodeBuild&) = delete;

    // Starts a build, configuring the build directory first when it was not yet, or for another toolchain, and again
    // when a CMake file changed or a file of code came or went, as globs of sources need. With a target, such as the
    // module alone, only it and what it needs are built. A build running already starts over once it ends, building
    // what both asked for.
    void Start(std::string_view target = {});
    // Follows the running build. Returns true the call it ends.
    bool Update();
    // Waits for the running build to end, following it.
    void Wait();

    // Whether the build directory was configured for this toolchain and these sources, so what it built works with
    // these tools.
    bool IsConfigured() const;
    // Whether the sources changed in a way that needs configuring again: a CMake file changed, or a file was added,
    // removed or renamed.
    bool NeedsReconfigure() const;

    CodeBuildState GetState() const { return m_state; }
    bool IsRunning() const { return m_state == CodeBuildState::Running; }
    std::size_t GetErrorCount() const { return m_errors; }
    std::size_t GetWarningCount() const { return m_warnings; }
    // How long the last build took, or the running one has taken so far.
    std::chrono::duration<float> GetDuration() const;
    // The phases of the last build that ended.
    const CodeBuildPhases& GetPhases() const { return m_phases; }
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
    // Changes with the names of the code's files and the CMake files' times.
    std::string StampSources() const;

    std::filesystem::path m_source;
    std::filesystem::path m_build;
    Toolchain m_toolchain;
    // Empty for everything.
    std::string m_target;
    std::optional<std::string> m_restartTarget;
    std::unique_ptr<Process> m_process;
    Step m_step = Step::Build;
    CodeBuildState m_state = CodeBuildState::Idle;
    std::size_t m_errors = 0;
    std::size_t m_warnings = 0;
    std::chrono::steady_clock::time_point m_started;
    std::chrono::steady_clock::time_point m_ended;
    // When the build step started, and when the build tool said it links.
    std::chrono::steady_clock::time_point m_buildStarted;
    std::optional<std::chrono::steady_clock::time_point> m_linkStarted;
    CodeBuildPhases m_phases;
    // Errors and warnings seen in this build, as builds that report them again at their end would show them twice.
    std::vector<std::string> m_reported;
    // CMake's messages span several lines: the first one and those indented below it.
    LogLevel m_messageLevel = LogLevel::Trace;
    std::string m_message;
    // The last lines that were neither errors nor warnings.
    std::deque<std::string> m_tail;
};

} // namespace Spot
