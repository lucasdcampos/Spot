#include "imgui/imgui_layer.h"

#include "imgui/editor_style.h"

#include <imgui.h>

namespace Spot::Editor {

ImGuiLayer::ImGuiLayer(Window& window, GraphicsDevice& device) : m_window(window) {
    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_context);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    // Windows move by their title bar only, so dragging inside panels never moves them by accident.
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigDragClickToInputText = true;

    m_platform = std::make_unique<ImGuiPlatform>(window);
    m_renderer = std::make_unique<ImGuiRenderer>(device, device.GetWindowFormat());
    LoadEditorFonts();
}

ImGuiLayer::~ImGuiLayer() {
    ImGui::SetCurrentContext(m_context);
    if (ImGui::GetIO().IniFilename) {
        ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
    }
    m_renderer.reset();
    m_platform.reset();
    ImGui::DestroyContext(m_context);
}

void ImGuiLayer::SetSettingsFile(const std::filesystem::path& path) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.IniFilename) {
        ImGui::SaveIniSettingsToDisk(io.IniFilename);
    }
    m_settingsFile = path.empty() ? std::string() : PathToUtf8(path);
    io.IniFilename = m_settingsFile.empty() ? nullptr : m_settingsFile.c_str();
    if (io.IniFilename) {
        ImGui::LoadIniSettingsFromDisk(io.IniFilename);
    }
}

void ImGuiLayer::ProcessEvent(const Event& event) {
    m_platform->ProcessEvent(event);
}

void ImGuiLayer::BeginFrame(float deltaTime) {
    // Applied on the first frame too, and again when the window moves to a display with another scale.
    if (const float scale = m_window.GetContentScale() * m_uiScale; scale != m_appliedScale) {
        m_appliedScale = scale;
        ApplyEditorStyle(scale);
    }
    m_platform->NewFrame(deltaTime);
    ImGui::NewFrame();
}

void ImGuiLayer::EndFrame() {
    ImGui::Render();
}

void ImGuiLayer::Render(RenderPass& pass) {
    if (const ImDrawData* drawData = ImGui::GetDrawData()) {
        m_renderer->Render(pass, *drawData);
    }
}

} // namespace Spot::Editor
