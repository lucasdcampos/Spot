#pragma once

#include <spot.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Spot::Editor {

// A project's game running outside of the editor, in its own window, as it will once shipped: the player runs it,
// and what it logs shows in the console as "Game".
class GameRunner {
public:
    // The player built along with the editor.
    static std::filesystem::path GetBuiltPlayer();

    explicit GameRunner(std::filesystem::path player = GetBuiltPlayer());
    // Stops the game.
    ~GameRunner();

    GameRunner(const GameRunner&) = delete;
    GameRunner& operator=(const GameRunner&) = delete;

    // Runs the project's game, with the player's options before it, stopping the game that runs. Returns false,
    // after logging why, when the player cannot start.
    bool Start(const std::filesystem::path& projectFile, std::span<const std::string> options = {});
    void Stop();
    bool IsRunning();
    // Its exit code once it exited; nothing while it runs, or before it ran.
    std::optional<int> GetExitCode();
    // Shows what the game logged since the last call, and tells when it exits.
    void Update();

    struct Line {
        LogLevel level = LogLevel::Info;
        // Prefixed by its logger's name, but for the game's own messages.
        std::string message;
    };
    // Reads a line of the player's log, "[14:03:12.345] [INFO ] [Spot] message"; lines that are not, such as those
    // a message spans, come back whole.
    static Line ParseLine(std::string_view line);

private:
    std::filesystem::path m_player;
    std::unique_ptr<Process> m_process;
    bool m_exitReported = false;
};

} // namespace Spot::Editor
