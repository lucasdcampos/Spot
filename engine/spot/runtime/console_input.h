#pragma once

#include "spot/platform/input_state.h"

namespace Spot {

class Console;

// Event-driven editing while InputState captures gameplay input. The caller manages platform text input.
class ConsoleInput {
public:
    ConsoleInput(Console& console, InputState& input);
    ~ConsoleInput();
    ConsoleInput(const ConsoleInput&) = delete;
    ConsoleInput& operator=(const ConsoleInput&) = delete;
    bool OnEvent(const Event& event);
    void SetOpen(bool open);
    bool IsOpen() const { return m_capture != InputCaptureId::None; }
    const std::string& GetLine() const { return m_line; }
    void SetLine(std::string line);
    void Submit();
    void Complete();
    void BrowseHistory(int direction);

private:
    Console& m_console;
    InputState& m_input;
    InputCaptureId m_capture = InputCaptureId::None;
    std::string m_line;
    std::string m_draft;
    std::size_t m_history = 0;
};

} // namespace Spot
