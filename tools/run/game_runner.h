#pragma once

#include <spot.h>

#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Spot {

// A game's executable running in its own window, as it will once shipped, from its own folder: what it logs is logged
// again as "Game", so it shows in the editor's console and the command line's output.
class GameRunner {
public:
    GameRunner() = default;
    // Stops the game.
    ~GameRunner();

    GameRunner(const GameRunner&) = delete;
    GameRunner& operator=(const GameRunner&) = delete;

    // Runs the game's executable with the arguments, stopping the game that runs. Returns false, after logging why,
    // when it cannot start.
    bool Start(const std::filesystem::path& executable, std::span<const std::string> arguments = {});
    void Stop();
    bool IsRunning();
    // Its exit code once it exited; nothing while it runs, or before it ran.
    std::optional<int> GetExitCode();
    // Shows what the game logged since the last call, and tells when it exits.
    void Update();
    // Bounded completed frames received over the game's log pipe, valid until Update() or Start().
    std::vector<const ProfileFrame*> GetProfileFrames() const;

    struct Line {
        LogLevel level = LogLevel::Info;
        // Prefixed by its logger's name, but for the game's own messages.
        std::string message;
    };
    // Reads a line of the game's log, "[14:03:12.345] [INFO ] [Spot] message"; lines that are not, such as those
    // a message spans, come back whole.
    static Line ParseLine(std::string_view line);

private:
    std::unique_ptr<Process> m_process;
    bool m_exitReported = false;
    std::deque<ProfileFrame> m_profileFrames;
};

} // namespace Spot
