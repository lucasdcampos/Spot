#include "core/process.h"

#include "core/log.h"
#include "core/paths.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <mutex>
#include <thread>
#include <utility>

namespace Spot {

struct Process::Impl {
    SDL_Process* process = nullptr;
    std::thread reader;
    std::mutex mutex;
    std::vector<std::string> lines;
    std::string partial;
    std::optional<int> exitCode;

    // Splits what was read into lines, keeping an unfinished one for the next read.
    void Append(std::string_view text) {
        const std::lock_guard lock(mutex);
        for (const char c : text) {
            if (c == '\n') {
                if (!partial.empty() && partial.back() == '\r') {
                    partial.pop_back();
                }
                lines.push_back(std::move(partial));
                partial.clear();
            } else {
                partial += c;
            }
        }
    }

    void Finish() {
        const std::lock_guard lock(mutex);
        if (!partial.empty()) {
            lines.push_back(std::move(partial));
            partial.clear();
        }
    }

    // Reads until the program closes its output, which SDL's streams report as not ready while it is quiet.
    void Read(SDL_IOStream* output) {
        char buffer[4096];
        for (;;) {
            const std::size_t read = SDL_ReadIO(output, buffer, sizeof(buffer));
            if (read > 0) {
                Append({buffer, read});
                continue;
            }
            if (SDL_GetIOStatus(output) != SDL_IO_STATUS_NOT_READY) {
                break;
            }
            SDL_Delay(5);
        }
        Finish();
    }
};

Process::Process(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

std::optional<std::string> Process::GetEnvironmentValue(const char* name) {
    const char* value = name ? SDL_GetEnvironmentVariable(SDL_GetEnvironment(), name) : nullptr;
    return value ? std::optional<std::string>(value) : std::nullopt;
}

std::unique_ptr<Process> Process::Start(std::span<const std::string> arguments,
                                        const std::filesystem::path& workingDirectory,
                                        std::span<const ProcessEnvironmentVariable> environment) {
    if (arguments.empty()) {
        Log::GetEngineLogger().Error("A process needs a program to run");
        return nullptr;
    }
    std::vector<const char*> argv;
    for (const std::string& argument : arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);
    const std::string directory = PathToUtf8(workingDirectory);

    std::unique_ptr<SDL_Environment, decltype(&SDL_DestroyEnvironment)> childEnvironment(nullptr,
                                                                                         SDL_DestroyEnvironment);
    if (!environment.empty()) {
        childEnvironment.reset(SDL_CreateEnvironment(true));
        if (!childEnvironment) {
            return nullptr;
        }
        for (const auto& variable : environment) {
            if (variable.name.empty() || variable.name.find('=') != std::string::npos ||
                variable.name.find('\0') != std::string::npos || variable.value.find('\0') != std::string::npos ||
                !SDL_SetEnvironmentVariable(childEnvironment.get(), variable.name.c_str(), variable.value.c_str(),
                                            true)) {
                Log::GetEngineLogger().Error("Invalid child process environment variable '{}': {}", variable.name,
                                             SDL_GetError());
                return nullptr;
            }
        }
    }

    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    if (childEnvironment) {
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, childEnvironment.get());
    }
    if (!workingDirectory.empty()) {
        SDL_SetStringProperty(properties, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, directory.c_str());
    }
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!process) {
        Log::GetEngineLogger().Error("Could not run '{}': {}", arguments.front(), SDL_GetError());
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->process = process;
    Impl* state = impl.get();
    SDL_IOStream* output = SDL_GetProcessOutput(process);
    impl->reader = std::thread([state, output] { state->Read(output); });
    return std::unique_ptr<Process>(new Process(std::move(impl)));
}

Process::~Process() {
    if (IsRunning()) {
        Kill();
    }
    Wait();
    SDL_DestroyProcess(m_impl->process);
}

std::vector<std::string> Process::TakeOutput() {
    const std::lock_guard lock(m_impl->mutex);
    return std::exchange(m_impl->lines, {});
}

bool Process::IsRunning() {
    return !GetExitCode().has_value();
}

std::optional<int> Process::GetExitCode() {
    if (!m_impl->exitCode) {
        int code = 0;
        if (SDL_WaitProcess(m_impl->process, false, &code)) {
            m_impl->exitCode = code;
        }
    }
    return m_impl->exitCode;
}

int Process::Wait() {
    if (!m_impl->exitCode) {
        int code = 0;
        SDL_WaitProcess(m_impl->process, true, &code);
        m_impl->exitCode = code;
    }
    // All it wrote is read once its output closes.
    if (m_impl->reader.joinable()) {
        m_impl->reader.join();
    }
    return *m_impl->exitCode;
}

void Process::Kill() {
    SDL_KillProcess(m_impl->process, true);
}

} // namespace Spot
