#pragma once

#include "spot/runtime/console.h"
#include "spot/runtime/module.h"

#include <istream>
#include <memory>
#include <ostream>

namespace Spot {

// Runs the lines typed in the terminal the application was started from as console commands, each frame on the main
// thread, and prints what they answer: for dedicated servers and tools without a window. `quit` stops the
// application. Lines are read on a thread of their own, which a terminal that closes (end of input) ends; one still
// waiting for a line when the application stops is left to end with the process.
class TerminalConsole final : public Module {
public:
    // The process's standard input and output unless others are given, as in tests.
    explicit TerminalConsole(Console* console = nullptr, std::istream* input = nullptr, std::ostream* output = nullptr);
    ~TerminalConsole() override;

    // Runs the lines that arrived, as each frame does.
    void Poll();
    // Whether the input ended, as when the terminal closed or what was piped in was all read.
    bool HasInputEnded() const;

protected:
    void OnStart() override;
    void OnStop() override;
    void OnUpdate(float deltaTime) override;

private:
    struct Lines;

    Console* m_console;
    std::istream* m_input;
    std::ostream* m_output;
    std::shared_ptr<Lines> m_lines;
    ConsoleRegistration m_quit;
};

} // namespace Spot
