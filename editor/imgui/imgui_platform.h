#pragma once

#include <spot.h>

#include <array>
#include <string>

namespace Spot::Editor {

// Feeds the current ImGui context with the window's events and keeps the window's cursor and text input in step
// with what ImGui asks for. Only the engine's public API is used, so any platform backend works.
class ImGuiPlatform {
public:
    explicit ImGuiPlatform(Window& window);
    ~ImGuiPlatform();

    ImGuiPlatform(const ImGuiPlatform&) = delete;
    ImGuiPlatform& operator=(const ImGuiPlatform&) = delete;

    void ProcessEvent(const Event& event);
    // Called before ImGui::NewFrame().
    void NewFrame(float deltaTime);

private:
    void UpdateModifier(Key key, bool down);
    void UpdateCursor();

    Window& m_window;
    // Left and right of Ctrl, Shift, Alt and Super, which ImGui needs merged into modifiers.
    std::array<bool, 8> m_modifierKeys{};
    std::string m_clipboard;
};

} // namespace Spot::Editor
