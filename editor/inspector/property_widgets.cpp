#include "inspector/property_widgets.h"

#include "core/text.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>

namespace Spot::Editor {

namespace {

// The text, clipped to the width, at the position, vertically centered in a frame.
void DrawClippedText(ImDrawList* drawList, ImVec2 min, ImVec2 max, const char* text, ImU32 color) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 position{min.x, min.y + (max.y - min.y - size.y) * 0.5f};
    drawList->PushClipRect(min, max, true);
    drawList->AddText(position, color, text);
    drawList->PopClipRect();
}

} // namespace

float LinearToSrgb(float value) {
    if (value <= 0.0031308f) {
        return std::max(value, 0.0f) * 12.92f;
    }
    return 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

float SrgbToLinear(float value) {
    if (value <= 0.04045f) {
        return std::max(value, 0.0f) / 12.92f;
    }
    return std::pow((value + 0.055f) / 1.055f, 2.4f);
}

std::optional<int> DragAxes(const char* id, float* values, int count, float speed, float min, float max,
                            const char* format, unsigned mixedMask) {
    static constexpr std::array AxisColors{IM_COL32(226, 76, 76, 255), IM_COL32(118, 185, 71, 255),
                                           IM_COL32(72, 136, 232, 255), IM_COL32(150, 150, 160, 255)};
    const ImGuiStyle& style = ImGui::GetStyle();
    const float spacing = style.ItemInnerSpacing.x;
    const float width =
        std::max(1.0f, (ImGui::CalcItemWidth() - spacing * static_cast<float>(count - 1)) / static_cast<float>(count));
    const float barWidth = std::round(3.0f * style.FontScaleDpi);
    const ImGuiSliderFlags flags = min < max ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;

    std::optional<int> changed;
    ImGui::PushID(id);
    ImGui::BeginGroup();
    for (int axis = 0; axis < count; ++axis) {
        if (axis > 0) {
            ImGui::SameLine(0.0f, spacing);
        }
        ImGui::PushID(axis);
        ImGui::SetNextItemWidth(width);
        const bool mixed = ((mixedMask >> axis) & 1u) != 0;
        if (ImGui::DragFloat("##axis", &values[axis], speed, min, max, mixed ? MixedValueText : format, flags)) {
            changed = axis;
        }
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(itemMin, {itemMin.x + barWidth, itemMax.y},
                                                  AxisColors[static_cast<std::size_t>(axis)], style.FrameRounding,
                                                  ImDrawFlags_RoundCornersLeft);
        ImGui::PopID();
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

Vector3 EulerCache::Get(std::uint64_t key, const Quaternion& rotation) const {
    const auto found = m_entries.find(key);
    if (found != m_entries.end()) {
        // q and -q are the same rotation.
        const Quaternion& cached = found->second.rotation;
        const auto near = [](float a, float b) { return std::abs(a - b) <= 1e-6f; };
        const bool same = near(cached.x, rotation.x) && near(cached.y, rotation.y) && near(cached.z, rotation.z) &&
                          near(cached.w, rotation.w);
        const bool opposite = near(cached.x, -rotation.x) && near(cached.y, -rotation.y) &&
                              near(cached.z, -rotation.z) && near(cached.w, -rotation.w);
        if (same || opposite) {
            return found->second.degrees;
        }
    }
    // Rounding leaves angles like -0.00001, which would show as "-0.0".
    const auto clean = [](float radians) {
        const float degrees = ToDegrees(radians);
        return std::abs(degrees) < 1e-3f ? 0.0f : degrees;
    };
    const Vector3 radians = ToEuler(rotation);
    return {clean(radians.x), clean(radians.y), clean(radians.z)};
}

void EulerCache::Set(std::uint64_t key, Vector3 degrees, const Quaternion& rotation) {
    m_entries[key] = {rotation, degrees};
}

PickerResult PickerField(const char* icon, std::string_view current, const char* payloadType,
                         const std::function<std::vector<PickerItem>()>& listItems, bool mixed,
                         const ImVec4* currentColor, const char* tooltip) {
    PickerResult result;
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 size{ImGui::CalcItemWidth(), ImGui::GetFrameHeight()};

    // Looks like an input, with what it refers to on the left and an arrow on the right.
    const bool clicked = ImGui::InvisibleButton("##picker", size);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImGuiCol background = ImGui::IsItemActive()    ? ImGuiCol_FrameBgActive
                                : ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered
                                                         : ImGuiCol_FrameBg;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(background), style.FrameRounding);
    const float arrowWidth = ImGui::CalcTextSize(ICON_FA_CHEVRON_DOWN).x;
    const ImVec2 textMin{min.x + style.FramePadding.x, min.y};
    const ImVec2 textMax{max.x - style.FramePadding.x * 2.0f - arrowWidth, max.y};
    std::string label;
    if (mixed) {
        label = MixedValueText;
    } else {
        label = std::string(icon) + "  " + std::string(current);
    }
    const ImU32 textColor = ImGui::GetColorU32(currentColor && !mixed ? *currentColor : style.Colors[ImGuiCol_Text]);
    DrawClippedText(drawList, textMin, textMax, label.c_str(), textColor);
    DrawClippedText(drawList, {max.x - style.FramePadding.x - arrowWidth, min.y}, max, ICON_FA_CHEVRON_DOWN,
                    ImGui::GetColorU32(EditorColors::TextMuted));
    if (tooltip && !mixed) {
        ImGui::SetItemTooltip("%s", tooltip);
    }
    if (ImGui::BeginDragDropTarget()) {
        result.dropped = ImGui::AcceptDragDropPayload(payloadType);
        ImGui::EndDragDropTarget();
    }

    // One list is open at a time, so they share their search.
    static std::string search;
    if (clicked) {
        search.clear();
        ImGui::OpenPopup("##pick");
    }
    ImGui::SetNextWindowPos({min.x, max.y}, ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints({std::max(size.x, 260.0f * style.FontScaleDpi), 0.0f},
                                        {FLT_MAX, 360.0f * style.FontScaleDpi});
    if (ImGui::BeginPopup("##pick")) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool entered = ImGui::InputTextWithHint("##search", ICON_FA_MAGNIFYING_GLASS "  Search", &search,
                                                      ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Separator();

        const std::vector<PickerItem> items = listItems();
        std::optional<int> first;
        const auto drawItem = [&](int index, const char* itemIcon, const std::string& itemLabel,
                                  const std::string& detail) {
            ImGui::PushID(index);
            if (ImGui::Selectable("##item")) {
                result.picked = index;
            }
            ImDrawList* itemDrawList = ImGui::GetWindowDrawList();
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            const std::string text = itemIcon ? std::string(itemIcon) + "  " + itemLabel : itemLabel;
            float detailWidth = 0.0f;
            if (!detail.empty()) {
                detailWidth = ImGui::CalcTextSize(detail.c_str()).x + style.ItemSpacing.x;
                DrawClippedText(itemDrawList, {itemMax.x - detailWidth + style.ItemSpacing.x, itemMin.y}, itemMax,
                                detail.c_str(), ImGui::GetColorU32(EditorColors::TextMuted));
            }
            DrawClippedText(itemDrawList, itemMin, {itemMax.x - detailWidth, itemMax.y}, text.c_str(),
                            ImGui::GetColorU32(ImGuiCol_Text));
            ImGui::PopID();
        };
        if (search.empty()) {
            drawItem(-1, nullptr, "None", {});
        }
        for (std::size_t i = 0; i < items.size(); ++i) {
            const PickerItem& item = items[i];
            if (!ContainsIgnoringCase(item.label, search) && !ContainsIgnoringCase(item.detail, search)) {
                continue;
            }
            if (!first) {
                first = static_cast<int>(i);
            }
            drawItem(static_cast<int>(i), item.icon, item.label, item.detail);
        }
        if (!first && !search.empty()) {
            ImGui::TextDisabled("Nothing matches");
        }
        if (entered && first) {
            result.picked = first;
        }
        if (result.picked) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return result;
}

} // namespace Spot::Editor
