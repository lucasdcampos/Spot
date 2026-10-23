#include "imgui/editor_style.h"

#include "imgui/icons.h"

#include "fa_solid_900.ttf.h"
#include "inter_regular.ttf.h"
#include "inter_semibold.ttf.h"

#include <cstdint>
#include <cstdio>
#include <span>

namespace Spot::Editor {

namespace {

ImFont* s_boldFont = nullptr;
ImFont* s_codeFont = nullptr;

ImFont* AddFont(std::span<const std::uint8_t> data, const char* name) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig config;
    // The data is embedded in the executable, so the atlas must not free it.
    config.FontDataOwnedByAtlas = false;
    std::snprintf(config.Name, sizeof(config.Name), "%s", name);
    ImFont* font = io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(data.data()), static_cast<int>(data.size()),
                                                  BaseFontSize, &config);

    ImFontConfig icons;
    icons.FontDataOwnedByAtlas = false;
    icons.MergeMode = true;
    // Icons share one width, so labels after them line up in lists and menus.
    icons.GlyphMinAdvanceX = BaseFontSize * 1.25f;
    icons.GlyphOffset = {0.0f, 0.5f};
    std::snprintf(icons.Name, sizeof(icons.Name), "%s", "Font Awesome 6 Free Solid");
    io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(Resources::FaSolid900Ttf.data()),
                                   static_cast<int>(Resources::FaSolid900Ttf.size()), BaseFontSize * 0.9f, &icons);
    return font;
}

ImVec4 Rgb(int hex, float alpha = 1.0f) {
    return {static_cast<float>((hex >> 16) & 0xff) / 255.0f, static_cast<float>((hex >> 8) & 0xff) / 255.0f,
            static_cast<float>(hex & 0xff) / 255.0f, alpha};
}

ImVec4 WithAlpha(ImVec4 color, float alpha) {
    color.w = alpha;
    return color;
}

void SetColors(ImGuiStyle& style) {
    ImVec4* colors = style.Colors;
    const ImVec4 accent = EditorColors::Accent;
    const ImVec4 background = Rgb(0x1f2024);
    const ImVec4 darker = Rgb(0x18191c);
    const ImVec4 surface = Rgb(0x2b2d33);
    const ImVec4 surfaceHovered = Rgb(0x34373e);
    const ImVec4 surfaceActive = Rgb(0x3c4048);
    const ImVec4 border = Rgb(0x34363c);

    colors[ImGuiCol_Text] = Rgb(0xdcdee3);
    colors[ImGuiCol_TextDisabled] = Rgb(0x787c86);
    colors[ImGuiCol_WindowBg] = background;
    colors[ImGuiCol_ChildBg] = {0.0f, 0.0f, 0.0f, 0.0f};
    colors[ImGuiCol_PopupBg] = Rgb(0x26282d, 0.98f);
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = {0.0f, 0.0f, 0.0f, 0.0f};
    colors[ImGuiCol_FrameBg] = surface;
    colors[ImGuiCol_FrameBgHovered] = surfaceHovered;
    colors[ImGuiCol_FrameBgActive] = surfaceActive;
    colors[ImGuiCol_TitleBg] = darker;
    colors[ImGuiCol_TitleBgActive] = darker;
    colors[ImGuiCol_TitleBgCollapsed] = darker;
    colors[ImGuiCol_MenuBarBg] = darker;
    colors[ImGuiCol_ScrollbarBg] = {0.0f, 0.0f, 0.0f, 0.0f};
    colors[ImGuiCol_ScrollbarGrab] = Rgb(0x3a3d44);
    colors[ImGuiCol_ScrollbarGrabHovered] = Rgb(0x484c55);
    colors[ImGuiCol_ScrollbarGrabActive] = Rgb(0x565b65);
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = Rgb(0x6aa7ff);
    colors[ImGuiCol_Button] = Rgb(0x2f3238);
    colors[ImGuiCol_ButtonHovered] = surfaceHovered;
    colors[ImGuiCol_ButtonActive] = surfaceActive;
    colors[ImGuiCol_Header] = Rgb(0x2f3238);
    colors[ImGuiCol_HeaderHovered] = surfaceHovered;
    colors[ImGuiCol_HeaderActive] = surfaceActive;
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = WithAlpha(accent, 0.7f);
    colors[ImGuiCol_SeparatorActive] = accent;
    colors[ImGuiCol_ResizeGrip] = {0.0f, 0.0f, 0.0f, 0.0f};
    colors[ImGuiCol_ResizeGripHovered] = WithAlpha(accent, 0.6f);
    colors[ImGuiCol_ResizeGripActive] = accent;
    colors[ImGuiCol_InputTextCursor] = colors[ImGuiCol_Text];
    colors[ImGuiCol_TabHovered] = surfaceHovered;
    colors[ImGuiCol_Tab] = darker;
    colors[ImGuiCol_TabSelected] = background;
    colors[ImGuiCol_TabSelectedOverline] = accent;
    colors[ImGuiCol_TabDimmed] = darker;
    colors[ImGuiCol_TabDimmedSelected] = background;
    colors[ImGuiCol_TabDimmedSelectedOverline] = WithAlpha(accent, 0.0f);
    colors[ImGuiCol_DockingPreview] = WithAlpha(accent, 0.5f);
    colors[ImGuiCol_DockingEmptyBg] = Rgb(0x141517);
    colors[ImGuiCol_PlotLines] = Rgb(0x9da3ad);
    colors[ImGuiCol_PlotLinesHovered] = accent;
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_PlotHistogramHovered] = Rgb(0x6aa7ff);
    colors[ImGuiCol_TableHeaderBg] = Rgb(0x26282d);
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = Rgb(0x2a2c31);
    colors[ImGuiCol_TableRowBg] = {0.0f, 0.0f, 0.0f, 0.0f};
    colors[ImGuiCol_TableRowBgAlt] = {1.0f, 1.0f, 1.0f, 0.02f};
    colors[ImGuiCol_TextLink] = Rgb(0x6aa7ff);
    colors[ImGuiCol_TextSelectedBg] = WithAlpha(accent, 0.35f);
    colors[ImGuiCol_TreeLines] = border;
    colors[ImGuiCol_DragDropTarget] = accent;
    colors[ImGuiCol_DragDropTargetBg] = WithAlpha(accent, 0.12f);
    colors[ImGuiCol_UnsavedMarker] = colors[ImGuiCol_Text];
    colors[ImGuiCol_NavCursor] = accent;
    colors[ImGuiCol_NavWindowingHighlight] = WithAlpha(accent, 0.7f);
    colors[ImGuiCol_NavWindowingDimBg] = {0.0f, 0.0f, 0.0f, 0.4f};
    colors[ImGuiCol_ModalWindowDimBg] = {0.0f, 0.0f, 0.0f, 0.5f};
}

} // namespace

void LoadEditorFonts() {
    ImGui::GetIO().Fonts->Clear();
    AddFont(Resources::InterRegularTtf, "Inter Regular");
    s_boldFont = AddFont(Resources::InterSemiboldTtf, "Inter SemiBold");
    s_codeFont = ImGui::GetIO().Fonts->AddFontDefaultVector();
}

ImFont* GetBoldFont() {
    return s_boldFont;
}

ImFont* GetCodeFont() {
    return ImGui::GetIO().Fonts->Fonts.contains(s_codeFont) ? s_codeFont : nullptr;
}

void ApplyEditorStyle(float contentScale) {
    ImGuiStyle style;
    style.FontSizeBase = BaseFontSize;
    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.45f;
    style.WindowPadding = {8.0f, 8.0f};
    style.WindowRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowMinSize = {64.0f, 48.0f};
    style.WindowTitleAlign = {0.0f, 0.5f};
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.ChildRounding = 4.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupRounding = 6.0f;
    style.PopupBorderSize = 1.0f;
    style.FramePadding = {8.0f, 4.0f};
    style.FrameRounding = 4.0f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = {8.0f, 5.0f};
    style.ItemInnerSpacing = {6.0f, 4.0f};
    style.CellPadding = {6.0f, 3.0f};
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 12.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabMinSize = 10.0f;
    style.GrabRounding = 3.0f;
    style.SelectableRounding = 3.0f;
    style.MenuItemRounding = 3.0f;
    style.TabRounding = 4.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 1.0f;
    style.TabBarOverlineSize = 2.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.DockingSeparatorSize = 2.0f;
    // Each tab has its own close button; one more for the whole node only invites closing panels by accident.
    style.DockingNodeHasCloseButton = false;
    style.SelectableTextAlign = {0.0f, 0.5f};
    SetColors(style);

    style.ScaleAllSizes(contentScale);
    style.FontScaleDpi = contentScale;
    ImGui::GetStyle() = style;
}

} // namespace Spot::Editor
