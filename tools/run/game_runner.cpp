#include "run/game_runner.h"
#include <spot/core/profile_stream.h>

#include <algorithm>
#include <array>
#include <format>
#include <utility>
#include <vector>

namespace Spot {

namespace {

Logger& GetGameLogger() {
    // The game decides what it logs; all of it shows.
    static Logger logger("Game", LogLevel::Trace);
    return logger;
}

// The logger the game's own messages, from Log::Info() and the others, come from.
constexpr std::string_view GameLoggerName = "App";

} // namespace

GameRunner::~GameRunner() {
    Stop();
    m_profileFrames.clear();
}

bool GameRunner::Start(const std::filesystem::path& executable, std::span<const std::string> arguments) {
    Stop();
    std::vector<std::string> command{PathToUtf8(executable)};
    command.insert(command.end(), arguments.begin(), arguments.end());
    const ProcessEnvironmentVariable environment[] = {{"SPOT_PROFILE_STREAM", "1"}};
    m_process = Process::Start(command, executable.parent_path(), environment);
    m_exitReported = false;
    if (!m_process) {
        GetGameLogger().Error("Could not start the game, '{}'", PathToUtf8(executable));
        return false;
    }
    GetGameLogger().Info("Started the game in its own window");
    return true;
}

void GameRunner::Stop() {
    if (m_process && m_process->IsRunning()) {
        m_process->Kill();
        m_process->Wait();
        Update();
    }
}

bool GameRunner::IsRunning() {
    return m_process && m_process->IsRunning();
}

std::optional<int> GameRunner::GetExitCode() {
    return m_process ? m_process->GetExitCode() : std::nullopt;
}

void GameRunner::Update() {
    if (!m_process) {
        return;
    }
    const bool exited = !m_process->IsRunning();
    if (exited) {
        // What it wrote last may still be on its way.
        m_process->Wait();
    }
    for (const std::string& text : m_process->TakeOutput()) {
        if (auto frame = DecodeProfileFrame(text)) {
            m_profileFrames.push_back(std::move(*frame));
            if (m_profileFrames.size() > Profiler::MaxFrames) {
                m_profileFrames.pop_front();
            }
            continue;
        }
        const Line line = ParseLine(text);
        GetGameLogger().Write(line.level, "{}", line.message);
    }
    if (exited && !m_exitReported) {
        m_exitReported = true;
        if (const int code = *m_process->GetExitCode(); code == 0) {
            GetGameLogger().Info("The game exited");
        } else {
            GetGameLogger().Warning("The game exited with code {}", code);
        }
    }
}

std::vector<const ProfileFrame*> GameRunner::GetProfileFrames() const {
    std::vector<const ProfileFrame*> frames;
    frames.reserve(m_profileFrames.size());
    for (const auto& frame : m_profileFrames) {
        frames.push_back(&frame);
    }
    return frames;
}

GameRunner::Line GameRunner::ParseLine(std::string_view line) {
    // "[time] [LEVEL] [Name] message", with the level padded to five characters.
    const auto close = [&](std::size_t from) {
        return from < line.size() && line[from] == '[' ? line.find(']', from) : std::string_view::npos;
    };
    const std::size_t timeEnd = close(0);
    const std::size_t levelEnd = timeEnd == std::string_view::npos ? timeEnd : close(timeEnd + 2);
    const std::size_t nameEnd = levelEnd == std::string_view::npos ? levelEnd : close(levelEnd + 2);
    if (nameEnd == std::string_view::npos || nameEnd + 1 >= line.size()) {
        return {.level = LogLevel::Info, .message = std::string(line)};
    }

    std::string_view level = line.substr(timeEnd + 3, levelEnd - timeEnd - 3);
    while (!level.empty() && level.back() == ' ') {
        level.remove_suffix(1);
    }
    constexpr std::array Levels{LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warning, LogLevel::Error};
    Line parsed;
    for (const LogLevel candidate : Levels) {
        if (GetLogLevelName(candidate) == level) {
            parsed.level = candidate;
        }
    }
    const std::string_view name = line.substr(levelEnd + 3, nameEnd - levelEnd - 3);
    const std::string_view message = line.substr(std::min(nameEnd + 2, line.size()));
    parsed.message = name == GameLoggerName ? std::string(message) : std::format("{}: {}", name, message);
    return parsed;
}

} // namespace Spot
