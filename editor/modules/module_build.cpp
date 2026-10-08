#include "modules/module_build.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace Spot::Editor {

namespace {

// The file in the build directory that says which toolchain configured it.
constexpr const char* ToolchainFile = "spot_toolchain.txt";
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

std::string ModuleToolchain::GetKey() const {
    // The configuration too, even for generators that build them all in one directory: the module goes to the same
    // file in each, and one built in another configuration does not work with this editor.
    return std::format("{}\n{}\n{}\n{}\n{}\n{}\n{}\n{}\n{}\n", cmake, generator, platform, toolset, instance, compiler,
                       makeProgram, configuration, PathToUtf8(package));
}

ModuleBuild::ModuleBuild(std::filesystem::path sourceDirectory, std::filesystem::path buildDirectory,
                         ModuleToolchain toolchain)
    : m_source(std::move(sourceDirectory)), m_build(std::move(buildDirectory)), m_toolchain(std::move(toolchain)) {
}

ModuleBuild::~ModuleBuild() = default;

void ModuleBuild::Start() {
    if (IsRunning()) {
        m_restart = true;
        return;
    }
    m_state = ModuleBuildState::Running;
    m_restart = false;
    m_errors = 0;
    m_warnings = 0;
    m_reported.clear();
    m_message.clear();
    m_tail.clear();
    m_started = std::chrono::steady_clock::now();

    if (IsConfigured()) {
        StartStep(Step::Build);
        return;
    }
    // A build directory is tied to its generator and compiler, so another toolchain needs a new one.
    std::error_code error;
    std::filesystem::remove_all(m_build, error);
    std::filesystem::create_directories(m_build, error);
    GetBuildLogger().Info("Configuring the game's code for {}", m_toolchain.generator);
    StartStep(Step::Configure);
}

void ModuleBuild::StartStep(Step step) {
    m_step = step;
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
    }

    m_process = Process::Start(arguments, m_source);
    if (!m_process) {
        GetBuildLogger().Error("Could not run CMake, '{}', to build the game's code", m_toolchain.cmake);
        Finish(false);
    }
}

bool ModuleBuild::Update() {
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
        StartStep(Step::Build);
        return false;
    }
    Finish(*exitCode == 0);
    if (m_restart) {
        Start();
        return false;
    }
    return true;
}

void ModuleBuild::Wait() {
    while (IsRunning()) {
        if (m_process) {
            m_process->Wait();
        }
        Update();
    }
}

bool ModuleBuild::IsConfigured() const {
    std::error_code error;
    return std::filesystem::exists(m_build / "CMakeCache.txt", error) &&
           ReadFile(m_build / ToolchainFile) == GetConfigurationKey();
}

std::string ModuleBuild::GetConfigurationKey() const {
    // CMake ties a build directory to its sources too, which move with the project, or out of the project's root.
    return m_toolchain.GetKey() + PathToUtf8(m_source) + "\n";
}

std::chrono::duration<float> ModuleBuild::GetDuration() const {
    return (IsRunning() ? std::chrono::steady_clock::now() : m_ended) - m_started;
}

std::filesystem::path ModuleBuild::GetModulePath() const {
    return m_build / "module" / ("game" + std::string(GameModule::FileExtension));
}

LogLevel ModuleBuild::ClassifyLine(std::string_view line) {
    if (line.starts_with("CMake Error") || Contains(line, ": error") || Contains(line, ": fatal error") ||
        Contains(line, "undefined reference to") || line.starts_with("LINK : fatal error")) {
        return LogLevel::Error;
    }
    if (line.starts_with("CMake Warning") || Contains(line, ": warning")) {
        return LogLevel::Warning;
    }
    return LogLevel::Trace;
}

void ModuleBuild::Log(const std::vector<std::string>& lines) {
    for (const std::string& line : lines) {
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

void ModuleBuild::FlushMessage() {
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

void ModuleBuild::Finish(bool succeeded) {
    m_ended = std::chrono::steady_clock::now();
    m_state = succeeded ? ModuleBuildState::Succeeded : ModuleBuildState::Failed;
    const float seconds = GetDuration().count();
    if (succeeded) {
        if (m_warnings > 0) {
            GetBuildLogger().Info("Built the game's code in {:.1f} s, with {} warning{}", seconds, m_warnings,
                                  m_warnings == 1 ? "" : "s");
        } else {
            GetBuildLogger().Info("Built the game's code in {:.1f} s", seconds);
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

} // namespace Spot::Editor
