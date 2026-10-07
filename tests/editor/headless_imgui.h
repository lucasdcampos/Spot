#pragma once

#include <imgui.h>

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

private:
    ImGuiContext* m_context = nullptr;
    ImGuiContext* m_previous = nullptr;
};

} // namespace Spot::Tests
