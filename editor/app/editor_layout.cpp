#include "app/editor_layout.h"

#include <imgui_internal.h>

namespace Spot::Editor {

namespace {

void Dock(const std::string& id, ImGuiID node) {
    // Windows are found by the id after "###", whatever their title.
    ImGui::DockBuilderDockWindow(("###" + id).c_str(), node);
}

} // namespace

ImGuiID BuildDefaultLayout(ImGuiID dockSpace, const LayoutWindows& windows) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockSpace);
    ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockSpace, viewport->WorkSize);

    ImGuiID scenes = dockSpace;
    const ImGuiID right = ImGui::DockBuilderSplitNode(scenes, ImGuiDir_Right, 0.24f, nullptr, &scenes);
    ImGuiID hierarchy = right;
    const ImGuiID inspector = ImGui::DockBuilderSplitNode(hierarchy, ImGuiDir_Down, 0.58f, nullptr, &hierarchy);
    ImGuiID assets = ImGui::DockBuilderSplitNode(scenes, ImGuiDir_Down, 0.30f, nullptr, &scenes);
    const ImGuiID console = ImGui::DockBuilderSplitNode(assets, ImGuiDir_Right, 0.40f, nullptr, &assets);

    for (const std::string& scene : windows.scenes) {
        Dock(scene, scenes);
    }
    if (!windows.code.empty()) {
        Dock(windows.code, scenes);
    }
    Dock(windows.hierarchy, hierarchy);
    Dock(windows.inspector, inspector);
    Dock(windows.assets, assets);
    Dock(windows.console, console);
    if (!windows.mixer.empty()) {
        Dock(windows.mixer, console);
    }
    if (!windows.profiler.empty()) {
        Dock(windows.profiler, console);
    }
    ImGui::DockBuilderFinish(dockSpace);
    return scenes;
}

} // namespace Spot::Editor
