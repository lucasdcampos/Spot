#include "headless_imgui.h"

#include "app/editor_layout.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"

#include <catch2/catch_test_macros.hpp>

#include <imgui_internal.h>

#include <string>

namespace {

ImGuiDockNode* GetWindowNode(const char* id) {
    const ImGuiWindow* window = ImGui::FindWindowByName((std::string("###") + id).c_str());
    REQUIRE(window != nullptr);
    REQUIRE(window->DockNode != nullptr);
    return window->DockNode;
}

void SubmitWindow(const char* id) {
    ImGui::Begin((std::string(id) + "###" + id).c_str());
    ImGui::End();
}

} // namespace

TEST_CASE("Toolbar buttons center their icons", "[editor][toolbar]") {
    Spot::Tests::HeadlessImGui imgui({1600.0f, 900.0f});
    Spot::Editor::LoadEditorFonts();
    Spot::Editor::ApplyEditorStyle(1.0f);

    imgui.BeginFrame();
    ImGui::SetNextWindowSize({800.0f, 600.0f});
    ImGui::Begin("ToolbarTest");

    const float size = ImGui::GetFrameHeight();
    const ImVec2 buttonSize = {size * 1.2f, size};

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0.0f, 0.0f});
    for (const char* icon : {ICON_FA_PLAY, ICON_FA_PAUSE, ICON_FA_FORWARD_STEP, ICON_FA_STOP, ICON_FA_HAMMER,
                             ICON_FA_ARROW_POINTER, ICON_FA_UP_DOWN_LEFT_RIGHT, ICON_FA_ROTATE, ICON_FA_CUBE}) {
        const int vtxStart = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        ImGui::Button(icon, buttonSize);
        const ImVec2 bMin = ImGui::GetItemRectMin();
        const ImVec2 bMax = ImGui::GetItemRectMax();
        const int vtxEnd = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        REQUIRE(vtxEnd >= vtxStart + 4);
        const auto& v0 = ImGui::GetWindowDrawList()->VtxBuffer[vtxEnd - 4];
        const auto& v2 = ImGui::GetWindowDrawList()->VtxBuffer[vtxEnd - 2];
        const float leftPad = v0.pos.x - bMin.x;
        const float rightPad = bMax.x - v2.pos.x;
        const float topPad = v0.pos.y - bMin.y;
        const float bottomPad = bMax.y - v2.pos.y;
        CHECK(std::abs(leftPad - rightPad) <= 1.0f);
        CHECK(std::abs(topPad - bottomPad) <= 1.0f);
    }
    ImGui::PopStyleVar();

    ImGui::End();
    imgui.EndFrame();
}

TEST_CASE("The default layout puts scenes in the middle and panels around them", "[editor][layout]") {
    Spot::Tests::HeadlessImGui imgui({1600.0f, 900.0f});
    const Spot::Editor::LayoutWindows windows{.scenes = {"Scene"},
                                              .hierarchy = "Hierarchy",
                                              .inspector = "Inspector",
                                              .assets = "Assets",
                                              .console = "Console",
                                              .code = "Code"};

    // Docking settles over a couple of frames.
    for (int frame = 0; frame < 3; ++frame) {
        imgui.BeginFrame();
        const ImGuiID dockSpace = ImGui::GetID("DockSpace");
        if (frame == 0) {
            Spot::Editor::BuildDefaultLayout(dockSpace, windows);
        }
        ImGui::DockSpaceOverViewport(dockSpace, ImGui::GetMainViewport());
        for (const char* id : {"Scene", "Code", "Hierarchy", "Inspector", "Assets", "Console"}) {
            SubmitWindow(id);
        }
        imgui.EndFrame();
    }

    const ImGuiDockNode* scene = GetWindowNode("Scene");
    CHECK(GetWindowNode("Code") == scene);
    const ImGuiDockNode* hierarchy = GetWindowNode("Hierarchy");
    const ImGuiDockNode* inspector = GetWindowNode("Inspector");
    const ImGuiDockNode* assets = GetWindowNode("Assets");
    const ImGuiDockNode* console = GetWindowNode("Console");

    CHECK(hierarchy->Pos.x >= scene->Pos.x + scene->Size.x);
    CHECK(inspector->Pos.x == hierarchy->Pos.x);
    CHECK(inspector->Pos.y >= hierarchy->Pos.y + hierarchy->Size.y);
    CHECK(assets->Pos.y >= scene->Pos.y + scene->Size.y);
    CHECK(console->Pos.y == assets->Pos.y);
    CHECK(console->Pos.x >= assets->Pos.x + assets->Size.x);
    // Scenes take most of the space.
    CHECK(scene->Size.x * scene->Size.y > hierarchy->Size.x * hierarchy->Size.y * 2.0f);
}
