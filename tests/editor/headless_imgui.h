#pragma once

#include <imgui.h>

#include <functional>
#include <utility>

namespace Spot::Tests {

// An ImGui context that runs frames without a renderer, for testing UI logic on machines without a GPU. Texture
// requests are left unanswered, which ImGui tolerates as long as nothing is drawn.
class HeadlessImGui {
public:
    explicit HeadlessImGui(ImVec2 displaySize = {1280.0f, 720.0f}) {
        m_previous = ImGui::GetCurrentContext();
        m_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_context);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = displaySize;
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        // Every event queued before a frame takes effect in it, so tests know where the mouse and keys are.
        io.ConfigInputTrickleEventQueue = false;
    }

    ~HeadlessImGui() {
        ImGui::DestroyContext(m_context);
        ImGui::SetCurrentContext(m_previous);
    }

    HeadlessImGui(const HeadlessImGui&) = delete;
    HeadlessImGui& operator=(const HeadlessImGui&) = delete;

    void BeginFrame() { ImGui::NewFrame(); }
    void EndFrame() { ImGui::Render(); }

    // What Frame() draws, and so what the input helpers below interact with.
    void SetDraw(std::function<void()> draw) { m_draw = std::move(draw); }

    void Frame() {
        BeginFrame();
        if (m_draw) {
            m_draw();
        }
        EndFrame();
    }

    // Moves the mouse to the point, then presses and releases the button there, holding the modifiers.
    void Click(ImVec2 point, ImGuiKeyChord modifiers = 0, ImGuiMouseButton button = ImGuiMouseButton_Left) {
        ImGuiIO& io = ImGui::GetIO();
        SetModifiers(modifiers, true);
        io.AddMousePosEvent(point.x, point.y);
        Frame();
        io.AddMouseButtonEvent(button, true);
        Frame();
        io.AddMouseButtonEvent(button, false);
        Frame();
        SetModifiers(modifiers, false);
        Frame();
    }

    // Presses and releases a key, with the modifiers of the chord held.
    void Press(ImGuiKeyChord chord) {
        ImGuiIO& io = ImGui::GetIO();
        const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
        SetModifiers(chord, true);
        io.AddKeyEvent(key, true);
        Frame();
        io.AddKeyEvent(key, false);
        SetModifiers(chord, false);
        Frame();
    }

    // Presses the left button at one point, moves to the other in steps, and releases it there.
    void Drag(ImVec2 from, ImVec2 to, int steps = 6) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(from.x, from.y);
        Frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        Frame();
        for (int step = 1; step <= steps; ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(steps);
            io.AddMousePosEvent(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
            Frame();
        }
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        Frame();
        Frame();
    }

    // Types the text into the focused field.
    void Type(const char* text) {
        ImGui::GetIO().AddInputCharactersUTF8(text);
        Frame();
    }

private:
    static void SetModifiers(ImGuiKeyChord chord, bool down) {
        ImGuiIO& io = ImGui::GetIO();
        if (chord & ImGuiMod_Ctrl) {
            io.AddKeyEvent(ImGuiMod_Ctrl, down);
        }
        if (chord & ImGuiMod_Shift) {
            io.AddKeyEvent(ImGuiMod_Shift, down);
        }
        if (chord & ImGuiMod_Alt) {
            io.AddKeyEvent(ImGuiMod_Alt, down);
        }
    }

    ImGuiContext* m_context = nullptr;
    ImGuiContext* m_previous = nullptr;
    std::function<void()> m_draw;
};

} // namespace Spot::Tests
