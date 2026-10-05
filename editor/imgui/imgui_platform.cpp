#include "imgui/imgui_platform.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <optional>

namespace Spot::Editor {

namespace {

ImGuiKey ToImGuiKey(Key key) {
    switch (key) {
    case Key::A:
        return ImGuiKey_A;
    case Key::B:
        return ImGuiKey_B;
    case Key::C:
        return ImGuiKey_C;
    case Key::D:
        return ImGuiKey_D;
    case Key::E:
        return ImGuiKey_E;
    case Key::F:
        return ImGuiKey_F;
    case Key::G:
        return ImGuiKey_G;
    case Key::H:
        return ImGuiKey_H;
    case Key::I:
        return ImGuiKey_I;
    case Key::J:
        return ImGuiKey_J;
    case Key::K:
        return ImGuiKey_K;
    case Key::L:
        return ImGuiKey_L;
    case Key::M:
        return ImGuiKey_M;
    case Key::N:
        return ImGuiKey_N;
    case Key::O:
        return ImGuiKey_O;
    case Key::P:
        return ImGuiKey_P;
    case Key::Q:
        return ImGuiKey_Q;
    case Key::R:
        return ImGuiKey_R;
    case Key::S:
        return ImGuiKey_S;
    case Key::T:
        return ImGuiKey_T;
    case Key::U:
        return ImGuiKey_U;
    case Key::V:
        return ImGuiKey_V;
    case Key::W:
        return ImGuiKey_W;
    case Key::X:
        return ImGuiKey_X;
    case Key::Y:
        return ImGuiKey_Y;
    case Key::Z:
        return ImGuiKey_Z;
    case Key::Digit0:
        return ImGuiKey_0;
    case Key::Digit1:
        return ImGuiKey_1;
    case Key::Digit2:
        return ImGuiKey_2;
    case Key::Digit3:
        return ImGuiKey_3;
    case Key::Digit4:
        return ImGuiKey_4;
    case Key::Digit5:
        return ImGuiKey_5;
    case Key::Digit6:
        return ImGuiKey_6;
    case Key::Digit7:
        return ImGuiKey_7;
    case Key::Digit8:
        return ImGuiKey_8;
    case Key::Digit9:
        return ImGuiKey_9;
    case Key::Escape:
        return ImGuiKey_Escape;
    case Key::Enter:
        return ImGuiKey_Enter;
    case Key::Tab:
        return ImGuiKey_Tab;
    case Key::Backspace:
        return ImGuiKey_Backspace;
    case Key::Space:
        return ImGuiKey_Space;
    case Key::Minus:
        return ImGuiKey_Minus;
    case Key::Equals:
        return ImGuiKey_Equal;
    case Key::LeftBracket:
        return ImGuiKey_LeftBracket;
    case Key::RightBracket:
        return ImGuiKey_RightBracket;
    case Key::Backslash:
        return ImGuiKey_Backslash;
    case Key::Semicolon:
        return ImGuiKey_Semicolon;
    case Key::Apostrophe:
        return ImGuiKey_Apostrophe;
    case Key::Grave:
        return ImGuiKey_GraveAccent;
    case Key::Comma:
        return ImGuiKey_Comma;
    case Key::Period:
        return ImGuiKey_Period;
    case Key::Slash:
        return ImGuiKey_Slash;
    case Key::IntlBackslash:
        return ImGuiKey_Oem102;
    case Key::CapsLock:
        return ImGuiKey_CapsLock;
    case Key::ScrollLock:
        return ImGuiKey_ScrollLock;
    case Key::NumLock:
        return ImGuiKey_NumLock;
    case Key::PrintScreen:
        return ImGuiKey_PrintScreen;
    case Key::Pause:
        return ImGuiKey_Pause;
    case Key::Menu:
        return ImGuiKey_Menu;
    case Key::F1:
        return ImGuiKey_F1;
    case Key::F2:
        return ImGuiKey_F2;
    case Key::F3:
        return ImGuiKey_F3;
    case Key::F4:
        return ImGuiKey_F4;
    case Key::F5:
        return ImGuiKey_F5;
    case Key::F6:
        return ImGuiKey_F6;
    case Key::F7:
        return ImGuiKey_F7;
    case Key::F8:
        return ImGuiKey_F8;
    case Key::F9:
        return ImGuiKey_F9;
    case Key::F10:
        return ImGuiKey_F10;
    case Key::F11:
        return ImGuiKey_F11;
    case Key::F12:
        return ImGuiKey_F12;
    case Key::Insert:
        return ImGuiKey_Insert;
    case Key::Delete:
        return ImGuiKey_Delete;
    case Key::Home:
        return ImGuiKey_Home;
    case Key::End:
        return ImGuiKey_End;
    case Key::PageUp:
        return ImGuiKey_PageUp;
    case Key::PageDown:
        return ImGuiKey_PageDown;
    case Key::Left:
        return ImGuiKey_LeftArrow;
    case Key::Right:
        return ImGuiKey_RightArrow;
    case Key::Up:
        return ImGuiKey_UpArrow;
    case Key::Down:
        return ImGuiKey_DownArrow;
    case Key::Numpad0:
        return ImGuiKey_Keypad0;
    case Key::Numpad1:
        return ImGuiKey_Keypad1;
    case Key::Numpad2:
        return ImGuiKey_Keypad2;
    case Key::Numpad3:
        return ImGuiKey_Keypad3;
    case Key::Numpad4:
        return ImGuiKey_Keypad4;
    case Key::Numpad5:
        return ImGuiKey_Keypad5;
    case Key::Numpad6:
        return ImGuiKey_Keypad6;
    case Key::Numpad7:
        return ImGuiKey_Keypad7;
    case Key::Numpad8:
        return ImGuiKey_Keypad8;
    case Key::Numpad9:
        return ImGuiKey_Keypad9;
    case Key::NumpadDecimal:
        return ImGuiKey_KeypadDecimal;
    case Key::NumpadDivide:
        return ImGuiKey_KeypadDivide;
    case Key::NumpadMultiply:
        return ImGuiKey_KeypadMultiply;
    case Key::NumpadSubtract:
        return ImGuiKey_KeypadSubtract;
    case Key::NumpadAdd:
        return ImGuiKey_KeypadAdd;
    case Key::NumpadEnter:
        return ImGuiKey_KeypadEnter;
    case Key::LeftShift:
        return ImGuiKey_LeftShift;
    case Key::LeftControl:
        return ImGuiKey_LeftCtrl;
    case Key::LeftAlt:
        return ImGuiKey_LeftAlt;
    case Key::LeftSuper:
        return ImGuiKey_LeftSuper;
    case Key::RightShift:
        return ImGuiKey_RightShift;
    case Key::RightControl:
        return ImGuiKey_RightCtrl;
    case Key::RightAlt:
        return ImGuiKey_RightAlt;
    case Key::RightSuper:
        return ImGuiKey_RightSuper;
    default:
        return ImGuiKey_None;
    }
}

int ToImGuiButton(MouseButton button) {
    switch (button) {
    case MouseButton::Left:
        return ImGuiMouseButton_Left;
    case MouseButton::Right:
        return ImGuiMouseButton_Right;
    case MouseButton::Middle:
        return ImGuiMouseButton_Middle;
    case MouseButton::X1:
        return 3;
    case MouseButton::X2:
        return 4;
    }
    return ImGuiMouseButton_Left;
}

std::optional<Cursor> ToCursor(ImGuiMouseCursor cursor) {
    switch (cursor) {
    case ImGuiMouseCursor_Arrow:
        return Cursor::Arrow;
    case ImGuiMouseCursor_TextInput:
        return Cursor::Text;
    case ImGuiMouseCursor_ResizeAll:
        return Cursor::Move;
    case ImGuiMouseCursor_ResizeNS:
        return Cursor::ResizeNs;
    case ImGuiMouseCursor_ResizeEW:
        return Cursor::ResizeEw;
    case ImGuiMouseCursor_ResizeNESW:
        return Cursor::ResizeNesw;
    case ImGuiMouseCursor_ResizeNWSE:
        return Cursor::ResizeNwse;
    case ImGuiMouseCursor_Hand:
        return Cursor::Hand;
    case ImGuiMouseCursor_Wait:
        return Cursor::Wait;
    case ImGuiMouseCursor_Progress:
        return Cursor::Progress;
    case ImGuiMouseCursor_NotAllowed:
        return Cursor::NotAllowed;
    default:
        return std::nullopt;
    }
}

// The index of a modifier key in ImGuiPlatform::m_modifierKeys, or -1.
int GetModifierIndex(Key key) {
    switch (key) {
    case Key::LeftControl:
        return 0;
    case Key::RightControl:
        return 1;
    case Key::LeftShift:
        return 2;
    case Key::RightShift:
        return 3;
    case Key::LeftAlt:
        return 4;
    case Key::RightAlt:
        return 5;
    case Key::LeftSuper:
        return 6;
    case Key::RightSuper:
        return 7;
    default:
        return -1;
    }
}

ImGuiPlatform& GetPlatform() {
    return *static_cast<ImGuiPlatform*>(ImGui::GetIO().BackendPlatformUserData);
}

} // namespace

ImGuiPlatform::ImGuiPlatform(Window& window) : m_window(window) {
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendPlatformUserData == nullptr && "A platform backend is already set");
    io.BackendPlatformUserData = this;
    io.BackendPlatformName = "spot";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    platformIo.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
        ImGuiPlatform& platform = GetPlatform();
        // ImGui keeps the pointer until the next call, so the text lives in the platform.
        platform.m_clipboard = Clipboard::GetText();
        return platform.m_clipboard.c_str();
    };
    platformIo.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) { Clipboard::SetText(text); };
    platformIo.Platform_SetImeDataFn = [](ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData* data) {
        Window& window = GetPlatform().m_window;
        const bool wanted = data->WantVisible || data->WantTextInput;
        if (wanted && !window.IsTextInputActive()) {
            window.StartTextInput();
        } else if (!wanted && window.IsTextInputActive()) {
            window.StopTextInput();
        }
        if (data->WantVisible) {
            window.SetTextInputArea({data->InputPos.x, data->InputPos.y}, {1.0f, data->InputLineHeight});
        }
    };
}

ImGuiPlatform::~ImGuiPlatform() {
    if (m_window.IsTextInputActive()) {
        m_window.StopTextInput();
    }
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformUserData = nullptr;
    io.BackendPlatformName = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;
    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    platformIo.Platform_GetClipboardTextFn = nullptr;
    platformIo.Platform_SetClipboardTextFn = nullptr;
    platformIo.Platform_SetImeDataFn = nullptr;
}

void ImGuiPlatform::ProcessEvent(const Event& event) {
    ImGuiIO& io = ImGui::GetIO();
    if (const auto* pressed = event.GetIf<KeyPressedEvent>()) {
        UpdateModifier(pressed->key, true);
        io.AddKeyEvent(ToImGuiKey(pressed->key), true);
    } else if (const auto* released = event.GetIf<KeyReleasedEvent>()) {
        UpdateModifier(released->key, false);
        io.AddKeyEvent(ToImGuiKey(released->key), false);
    } else if (const auto* text = event.GetIf<TextInputEvent>()) {
        io.AddInputCharactersUTF8(text->text.c_str());
    } else if (const auto* moved = event.GetIf<MouseMovedEvent>()) {
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent(moved->position.x, moved->position.y);
    } else if (const auto* buttonPressed = event.GetIf<MouseButtonPressedEvent>()) {
        io.AddMouseButtonEvent(ToImGuiButton(buttonPressed->button), true);
    } else if (const auto* buttonReleased = event.GetIf<MouseButtonReleasedEvent>()) {
        io.AddMouseButtonEvent(ToImGuiButton(buttonReleased->button), false);
    } else if (const auto* scrolled = event.GetIf<MouseScrolledEvent>()) {
        // ImGui scrolls left for positive x, the engine reports positive x to the right.
        io.AddMouseWheelEvent(-scrolled->delta.x, scrolled->delta.y);
    } else if (event.Is<MouseLeaveEvent>()) {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    } else if (const auto* focus = event.GetIf<WindowFocusEvent>()) {
        io.AddFocusEvent(focus->focused);
        if (!focus->focused) {
            // Keys released while another window had focus never arrive.
            m_modifierKeys.fill(false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            io.AddKeyEvent(ImGuiMod_Shift, false);
            io.AddKeyEvent(ImGuiMod_Alt, false);
            io.AddKeyEvent(ImGuiMod_Super, false);
        }
    }
}

void ImGuiPlatform::NewFrame(float deltaTime) {
    ImGuiIO& io = ImGui::GetIO();
    const Vector2 size = m_window.GetSize();
    io.DisplaySize = {size.x, size.y};
    if (size.x > 0.0f && size.y > 0.0f) {
        io.DisplayFramebufferScale = {static_cast<float>(m_window.GetPixelWidth()) / size.x,
                                      static_cast<float>(m_window.GetPixelHeight()) / size.y};
    }
    // The first frame of an application can take no time at all, which ImGui rejects.
    io.DeltaTime = std::max(deltaTime, 1.0f / 10000.0f);
    UpdateCursor();
}

void ImGuiPlatform::UpdateModifier(Key key, bool down) {
    const int index = GetModifierIndex(key);
    if (index < 0) {
        return;
    }
    m_modifierKeys[static_cast<std::size_t>(index)] = down;
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, m_modifierKeys[0] || m_modifierKeys[1]);
    io.AddKeyEvent(ImGuiMod_Shift, m_modifierKeys[2] || m_modifierKeys[3]);
    io.AddKeyEvent(ImGuiMod_Alt, m_modifierKeys[4] || m_modifierKeys[5]);
    io.AddKeyEvent(ImGuiMod_Super, m_modifierKeys[6] || m_modifierKeys[7]);
}

void ImGuiPlatform::UpdateCursor() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) {
        return;
    }
    // A captured mouse belongs to whoever captured it, such as a camera being flown.
    if (m_window.IsMouseCaptured()) {
        return;
    }
    const std::optional<Cursor> cursor = io.MouseDrawCursor ? std::nullopt : ToCursor(ImGui::GetMouseCursor());
    if (!cursor) {
        if (m_window.IsCursorVisible()) {
            m_window.SetCursorVisible(false);
        }
        return;
    }
    if (!m_window.IsCursorVisible()) {
        m_window.SetCursorVisible(true);
    }
    if (m_window.GetCursor() != *cursor) {
        m_window.SetCursor(*cursor);
    }
}

} // namespace Spot::Editor
