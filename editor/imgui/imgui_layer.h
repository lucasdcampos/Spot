#pragma once

#include "imgui/imgui_platform.h"
#include "imgui/imgui_renderer.h"

#include <spot.h>

#include <filesystem>
#include <memory>
#include <string>

struct ImGuiContext;

namespace Spot::Editor {

// An ImGui context with docking, the editor's fonts and theme, fed by a window and drawn with a graphics device.
// It is made current on creation.
class ImGuiLayer {
public:
    ImGuiLayer(Window& window, GraphicsDevice& device);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    // Where window layouts are saved and loaded from; empty keeps them in memory only.
    void SetSettingsFile(const std::filesystem::path& path);

    void ProcessEvent(const Event& event);
    // Starts a frame; UI can be submitted until EndFrame().
    void BeginFrame(float deltaTime);
    void EndFrame();
    // Draws the frame ended last into the pass, which must cover the whole window.
    void Render(RenderPass& pass);

private:
    Window& m_window;
    ImGuiContext* m_context = nullptr;
    std::unique_ptr<ImGuiPlatform> m_platform;
    std::unique_ptr<ImGuiRenderer> m_renderer;
    std::string m_settingsFile;
    float m_contentScale = 0.0f;
};

} // namespace Spot::Editor
