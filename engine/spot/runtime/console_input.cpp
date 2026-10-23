#include "spot/runtime/console_input.h"

#include "spot/core/log.h"
#include "spot/runtime/console.h"

#include <algorithm>

namespace Spot {

ConsoleInput::ConsoleInput(Console& console, InputState& input) : m_console(console), m_input(input) {
    m_history = console.GetHistory().size();
}
ConsoleInput::~ConsoleInput() {
    SetOpen(false);
}

void ConsoleInput::SetOpen(bool open) {
    if (open == IsOpen()) {
        return;
    }
    if (open) {
        m_capture = m_input.PushCapture("Console");
        m_history = m_console.GetHistory().size();
    } else {
        m_input.PopCapture(m_capture);
        m_capture = InputCaptureId::None;
    }
}

void ConsoleInput::SetLine(std::string line) {
    m_line = std::move(line);
    m_history = m_console.GetHistory().size();
}

void ConsoleInput::Submit() {
    if (m_line.empty()) {
        return;
    }
    const auto result = m_console.Execute(m_line);
    if (result.success) {
        Log::Info("{}", result.message);
    } else {
        Log::Warning("{}", result.message);
    }
    m_line.clear();
    m_draft.clear();
    m_history = m_console.GetHistory().size();
}

void ConsoleInput::Complete() {
    if (m_line.find_first_of(" \t") != std::string::npos) {
        return;
    }
    auto matches = m_console.Complete(m_line);
    if (matches.empty()) {
        return;
    }
    auto common = matches.front();
    for (const auto& match : matches) {
        const auto length =
            std::mismatch(common.begin(), common.end(), match.begin(), match.end()).first - common.begin();
        common.resize(static_cast<std::size_t>(length));
    }
    SetLine(common + (matches.size() == 1 ? " " : ""));
}

void ConsoleInput::BrowseHistory(int direction) {
    const auto& history = m_console.GetHistory();
    m_history = std::min(m_history, history.size());
    if (direction < 0 && m_history > 0) {
        if (m_history == history.size()) {
            m_draft = m_line;
        }
        m_line = history[--m_history];
    } else if (direction > 0 && m_history < history.size()) {
        ++m_history;
        m_line = m_history == history.size() ? m_draft : history[m_history];
    }
}

bool ConsoleInput::OnEvent(const Event& event) {
    if (const auto* key = event.GetIf<KeyPressedEvent>(); key && key->key == Key::Grave && !key->repeat) {
        SetOpen(!IsOpen());
        return true;
    }
    if (!IsOpen()) {
        return false;
    }
    if (const auto* focus = event.GetIf<WindowFocusEvent>(); focus && !focus->focused) {
        SetOpen(false);
        return false;
    }
    if (const auto* text = event.GetIf<TextInputEvent>()) {
        if (!(m_line.empty() && (text->text == "`" || text->text == "~"))) {
            m_line += text->text;
        }
        m_history = m_console.GetHistory().size();
        return true;
    }
    if (const auto* key = event.GetIf<KeyPressedEvent>()) {
        switch (key->key) {
        case Key::Escape:
            SetOpen(false);
            break;
        case Key::Enter:
            Submit();
            break;
        case Key::Tab:
            Complete();
            break;
        case Key::Up:
            BrowseHistory(-1);
            break;
        case Key::Down:
            BrowseHistory(1);
            break;
        case Key::Backspace:
            if (!m_line.empty()) {
                auto last = m_line.size() - 1;
                while (last > 0 && (static_cast<unsigned char>(m_line[last]) & 0xC0) == 0x80) {
                    --last;
                }
                m_line.resize(last);
            }
            break;
        default:
            break;
        }
        return true;
    }
    return event.Is<KeyReleasedEvent>() || event.Is<TextCompositionEvent>() || event.Is<MouseMovedEvent>() ||
           event.Is<MouseButtonPressedEvent>() || event.Is<MouseButtonReleasedEvent>() ||
           event.Is<MouseScrolledEvent>() || event.Is<GamepadButtonPressedEvent>() ||
           event.Is<GamepadButtonReleasedEvent>() || event.Is<GamepadAxisEvent>();
}

} // namespace Spot
