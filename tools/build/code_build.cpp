#include "build/code_build.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <thread>
#include <utility>

namespace Spot {

namespace {

// The files in the build directory that say which toolchain configured it, and for which sources.
constexpr const char* ToolchainFile = "spot_toolchain.txt";
constexpr const char* SourcesFile = "spot_sources.txt";
// Lines kept to explain a failed build that reported no error the editor recognizes.
constexpr std::size_t TailLines = 30;

Logger& GetBuildLogger() {
    // Everything the build writes is kept, but only its errors and warnings stand out.
    static Logger logger("Build", LogLevel::Trace);
    return logger;
}

bool Contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

bool IsCMakeMessage(std::string_view line) {
    return line.starts_with("CMake Error") || line.starts_with("CMake Warning");
}

// MSBuild ends its messages with the project they came from, which says nothing to the game's author.
std::string_view TrimProject(std::string_view line) {
    if (line.ends_with(".vcxproj]")) {
        if (const std::size_t open = line.rfind(" ["); open != std::string_view::npos) {
            return line.substr(0, open);
        }
    }
    return line;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

std::string Toolchain::GetKey() const {
    // The configuration too, even for generators that build them all in one directory: the module goes to the same
    // file in each, and one built in another configuration does not work with this editor.
    return std::format("{}\n{}\n{}\n{}\n{}\n{}\n{}\n{}\n{}\n", cmake, generator, platform, toolset, instance, compiler,
                       makeProgram, configuration, PathToUtf8(package));
}

bool Toolchain::IsAvailable() const {
    std::error_code error;
    return !cmake.empty() && std::filesystem::is_regular_file(Utf8ToPath(cmake), error) &&
           (compiler.empty() || std::filesystem::is_regular_file(Utf8ToPath(compiler), error));
}

CodeBuild::CodeBuild(std::filesystem::path sourceDirectory, std::filesystem::path buildDirectory, Toolchain toolchain)
    : m_source(std::move(sourceDirectory)), m_build(std::move(buildDirectory)), m_toolchain(std::move(toolchain)) {
}

CodeBuild::~CodeBuild() = default;

void CodeBuild::Start(std::string_view target) {
    if (IsRunning()) {
        // Everything covers any target, and two different targets need everything too.
        m_restartTarget = !m_restartTarget || *m_restartTarget == target ? std::string(target) : std::string();
        return;
    }
    m_state = CodeBuildState::Running;
    m_target = target;
    m_restartTarget.reset();
    m_errors = 0;
    m_warnings = 0;
    m_reported.clear();
    m_message.clear();
    m_tail.clear();
    m_started = std::chrono::steady_clock::now();

    if (IsConfigured()) {
        // CMake would notice a changed CMake file itself, but a source added to a glob only once a build of the
        // Visual Studio generator already started from the old project, which then fails.
        if (NeedsReconfigure()) {
            GetBuildLogger().Info("Configuring the game's code again, as its files changed");
            StartStep(Step::Configure);
        } else {
            StartStep(Step::Build);
        }
        return;
    }
    // A build directory is tied to its generator and compiler, so another toolchain needs a new one.
    std::error_code error;
    std::filesystem::remove_all(m_build, error);
    std::filesystem::create_directories(m_build, error);
    GetBuildLogger().Info("Configuring the game's code for {}", m_toolchain.generator);
    StartStep(Step::Configure);
}

void CodeBuild::StartStep(Step step) {
    m_step = step;
    if (step == Step::Build) {
        m_buildStarted = std::chrono::steady_clock::now();
        m_linkStarted.reset();
    }
    std::vector<std::string> arguments{m_toolchain.cmake};
    if (step == Step::Configure) {
        arguments.insert(arguments.end(),
                         {"-S", PathToUtf8(m_source), "-B", PathToUtf8(m_build), "-G", m_toolchain.generator});
        if (!m_toolchain.platform.empty()) {
            arguments.insert(arguments.end(), {"-A", m_toolchain.platform});
        }
        if (!m_toolchain.toolset.empty()) {
            arguments.insert(arguments.end(), {"-T", m_toolchain.toolset});
        }
        if (!m_toolchain.instance.empty()) {
            arguments.push_back("-DCMAKE_GENERATOR_INSTANCE=" + m_toolchain.instance);
        }
        // Generators of IDEs pick their own compiler; the others take the editor's.
        const bool ide = m_toolchain.generator.starts_with("Visual Studio") || m_toolchain.generator == "Xcode";
        if (!ide && !m_toolchain.compiler.empty()) {
            arguments.push_back("-DCMAKE_CXX_COMPILER=" + m_toolchain.compiler);
        }
        if (!ide && !m_toolchain.makeProgram.empty()) {
            arguments.push_back("-DCMAKE_MAKE_PROGRAM=" + m_toolchain.makeProgram);
        }
        if (!m_toolchain.multiConfiguration) {
            arguments.push_back("-DCMAKE_BUILD_TYPE=" + m_toolchain.configuration);
        }
        // For code editors that read it, such as those using clangd.
        arguments.push_back("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON");
        arguments.push_back("-DSpot_DIR=" + PathToUtf8(m_toolchain.package));
    } else {
        arguments.insert(arguments.end(),
                         {"--build", PathToUtf8(m_build), "--config", m_toolchain.configuration, "--parallel"});
        if (!m_target.empty()) {
            arguments.insert(arguments.end(), {"--target", m_target});
        }
    }

    m_process = Process::Start(arguments, m_source);
    if (!m_process) {
        GetBuildLogger().Error("Could not run CMake, '{}', to build the game's code", m_toolchain.cmake);
        Finish(false);
    }
}

bool CodeBuild::Update() {
    if (!IsRunning() || !m_process) {
        return false;
    }
    Log(m_process->TakeOutput());
    const std::optional<int> exitCode = m_process->GetExitCode();
    if (!exitCode) {
        return false;
    }
    // What it wrote before exiting may still be on its way.
    m_process->Wait();
    Log(m_process->TakeOutput());
    FlushMessage();
    m_process.reset();

    if (*exitCode == 0 && m_step == Step::Configure) {
        std::ofstream(m_build / ToolchainFile, std::ios::binary) << GetConfigurationKey();
        std::ofstream(m_build / SourcesFile, std::ios::binary) << StampSources();
        StartStep(Step::Build);
        return false;
    }
    Finish(*exitCode == 0);
    if (m_restartTarget) {
        Start(std::exchange(m_restartTarget, std::nullopt).value());
        return false;
    }
    return true;
}

void CodeBuild::Wait() {
    // Followed as it goes rather than once it exits, so the phases are timed as the output comes.
    while (IsRunning()) {
        if (!Update() && IsRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

bool CodeBuild::IsConfigured() const {
    std::error_code error;
    return std::filesystem::exists(m_build / "CMakeCache.txt", error) &&
           ReadFile(m_build / ToolchainFile) == GetConfigurationKey();
}

bool CodeBuild::NeedsReconfigure() const {
    return ReadFile(m_build / SourcesFile) != StampSources();
}

std::string CodeBuild::StampSources() const {
    std::vector<std::string> lines;
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(m_source, error)) {
        if (!entry.is_regular_file(error)) {
            continue;
        }
        const std::filesystem::path& path = entry.path();
        std::string line = PathToUtf8(path.lexically_relative(m_source));
        // CMake files by their time too, as any change to them may change the build.
        if (path.filename() == "CMakeLists.txt" || path.extension() == ".cmake") {
            line += std::format(" {}", entry.last_write_time(error).time_since_epoch().count());
        }
        lines.push_back(std::move(line));
    }
    std::ranges::sort(lines);
    std::string stamp;
    for (const std::string& line : lines) {
        stamp += line;
        stamp += '\n';
    }
    return stamp;
}

std::string CodeBuild::GetConfigurationKey() const {
    // CMake ties a build directory to its sources too, which move with the project, or out of the project's root.
    return m_toolchain.GetKey() + PathToUtf8(m_source) + "\n";
}

std::chrono::duration<float> CodeBuild::GetDuration() const {
    return (IsRunning() ? std::chrono::steady_clock::now() : m_ended) - m_started;
}

std::filesystem::path CodeBuild::GetModulePath() const {
    return m_build / "module" / ("game" + std::string(GameModule::FileExtension));
}

LogLevel CodeBuild::ClassifyLine(std::string_view line) {
    if (line.starts_with("CMake Error") || Contains(line, ": error") || Contains(line, ": fatal error") ||
        Contains(line, "undefined reference to") || line.starts_with("LINK : fatal error")) {
        return LogLevel::Error;
    }
    if (line.starts_with("CMake Warning") || Contains(line, ": warning")) {
        return LogLevel::Warning;
    }
    return LogLevel::Trace;
}

void CodeBuild::Log(const std::vector<std::string>& lines) {
    for (const std::string& line : lines) {
        // Ninja and Make say when they link, "Linking CXX shared module ...", and the MSVC linker as it makes the
        // module's import library, "Creating library ...".
        if (m_step == Step::Build && !m_linkStarted &&
            (Contains(line, "Linking ") || Contains(line, "Creating library"))) {
            m_linkStarted = std::chrono::steady_clock::now();
        }
        // Lines below a CMake message, indented or blank, are part of it.
        if (!m_message.empty() && (line.empty() || line.front() == ' ')) {
            m_message += '\n';
            m_message += line;
            continue;
        }
        FlushMessage();

        const LogLevel level = ClassifyLine(line);
        if (level == LogLevel::Trace) {
            GetBuildLogger().Trace("{}", line);
            m_tail.push_back(line);
            if (m_tail.size() > TailLines) {
                m_tail.pop_front();
            }
            continue;
        }
        if (IsCMakeMessage(line)) {
            m_messageLevel = level;
            m_message = line;
            continue;
        }
        const std::string message(TrimProject(line));
        if (std::ranges::find(m_reported, message) != m_reported.end()) {
            continue;
        }
        m_reported.push_back(message);
        (level == LogLevel::Error ? m_errors : m_warnings) += 1;
        GetBuildLogger().Write(level, "{}", message);
    }
}

void CodeBuild::FlushMessage() {
    if (m_message.empty()) {
        return;
    }
    while (!m_message.empty() && (m_message.back() == '\n' || m_message.back() == ' ')) {
        m_message.pop_back();
    }
    (m_messageLevel == LogLevel::Error ? m_errors : m_warnings) += 1;
    GetBuildLogger().Write(m_messageLevel, "{}", m_message);
    m_message.clear();
}

void CodeBuild::Finish(bool succeeded) {
    m_ended = std::chrono::steady_clock::now();
    m_state = succeeded ? CodeBuildState::Succeeded : CodeBuildState::Failed;
    const bool built = m_step == Step::Build && m_buildStarted >= m_started;
    m_phases = {};
    if (built) {
        m_phases.configure = m_buildStarted - m_started;
        const auto compileEnd = m_linkStarted.value_or(m_ended);
        m_phases.compile = compileEnd - m_buildStarted;
        m_phases.link = m_ended - compileEnd;
        m_phases.linkMeasured = m_linkStarted.has_value();
    }
    const float seconds = GetDuration().count();
    if (succeeded) {
        const std::string phases =
            m_phases.linkMeasured
                ? std::format("configure {:.1f} s, compile {:.1f} s, link {:.1f} s", m_phases.configure.count(),
                              m_phases.compile.count(), m_phases.link.count())
                : std::format("configure {:.1f} s, compile and link {:.1f} s", m_phases.configure.count(),
                              m_phases.compile.count());
        if (m_warnings > 0) {
            GetBuildLogger().Info("Built the game's code in {:.1f} s ({}), with {} warning{}", seconds, phases,
                                  m_warnings, m_warnings == 1 ? "" : "s");
        } else {
            GetBuildLogger().Info("Built the game's code in {:.1f} s ({})", seconds, phases);
        }
    } else if (m_errors > 0) {
        GetBuildLogger().Error("The game's code did not build: {} error{}", m_errors, m_errors == 1 ? "" : "s");
    } else {
        // Without an error it recognizes, what the build wrote last explains best what went wrong.
        std::string tail;
        for (const std::string& line : m_tail) {
            tail += '\n';
            tail += line;
        }
        GetBuildLogger().Error("The game's code did not build:{}", tail);
    }
}

} // namespace Spot
