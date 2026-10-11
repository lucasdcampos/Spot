#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Spot {

struct ProcessEnvironmentVariable {
    std::string name;
    std::string value;
};

// Another program running alongside, as tools run compilers. What it writes to its standard output and error comes
// back as lines, read on a thread of its own, so nothing here waits unless asked to.
class Process {
public:
    // Current process resident memory; zero if the platform cannot query it. Includes CPU-side backend allocations.
    static std::uint64_t GetResidentMemoryBytes();
    // A copy of the current process environment value, or nothing if unset.
    static std::optional<std::string> GetEnvironmentValue(const char* name);
    // Runs the program, the first argument, with the others, in the working directory or the current one. Null, after
    // logging why, when it cannot start.
    static std::unique_ptr<Process> Start(std::span<const std::string> arguments,
                                          const std::filesystem::path& workingDirectory = {},
                                          std::span<const ProcessEnvironmentVariable> environment = {});
    // Kills the program if it still runs.
    ~Process();

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // The lines it wrote since the last call, without their line ends.
    std::vector<std::string> TakeOutput();
    bool IsRunning();
    // Its exit code once it exited; nothing while it runs.
    std::optional<int> GetExitCode();
    // Waits for it to exit, and returns its exit code.
    int Wait();
    void Kill();

private:
    struct Impl;

    explicit Process(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
