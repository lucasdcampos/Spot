#include "spot/runtime/terminal_console.h"

#include "spot/runtime/application.h"

#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace Spot {

// Shared with the reading thread, which may outlive the module while it waits for a line.
struct TerminalConsole::Lines {
    std::mutex mutex;
    std::deque<std::string> lines;
    bool ended = false;
};

TerminalConsole::TerminalConsole(Console* console, std::istream* input, std::ostream* output)
    : m_console(console ? console : &Console::Get()), m_input(input ? input : &std::cin),
      m_output(output ? output : &std::cout) {
}

TerminalConsole::~TerminalConsole() = default;

void TerminalConsole::OnStart() {
    m_lines = std::make_shared<Lines>();
    m_quit = m_console->Register(
        {"quit", "Stops the application", ConsoleFlags::None, [this](std::span<const std::string>) -> ConsoleResult {
             GetApplication().QuitGame();
             return {true, "Stopping"};
         }});
    std::thread([lines = m_lines, input = m_input] {
        std::string line;
        while (std::getline(*input, line)) {
            const std::lock_guard lock(lines->mutex);
            lines->lines.push_back(std::move(line));
        }
        const std::lock_guard lock(lines->mutex);
        lines->ended = true;
    }).detach();
    *m_output << "Type commands here; help lists them." << std::endl;
}

void TerminalConsole::OnStop() {
    m_quit.Reset();
    m_lines.reset();
}

bool TerminalConsole::HasInputEnded() const {
    if (!m_lines) {
        return true;
    }
    const std::lock_guard lock(m_lines->mutex);
    return m_lines->ended;
}

void TerminalConsole::OnUpdate(float /*deltaTime*/) {
    Poll();
}

void TerminalConsole::Poll() {
    if (!m_lines) {
        return;
    }
    std::deque<std::string> lines;
    {
        const std::lock_guard lock(m_lines->mutex);
        lines.swap(m_lines->lines);
    }
    for (std::string& line : lines) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }
        const ConsoleResult result = m_console->Execute(line);
        if (!result.message.empty()) {
            *m_output << (result.success ? "" : "Error: ") << result.message << std::endl;
        }
    }
}

} // namespace Spot
