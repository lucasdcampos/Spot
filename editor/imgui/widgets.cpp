#include "imgui/widgets.h"

#include "imgui/editor_style.h"

#include <cfloat>
#include <format>

namespace Spot::Editor::Widgets {

bool BeginProperties(const char* id) {
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
    if (!ImGui::BeginTable(id, 2, flags)) {
        return false;
    }
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    return true;
}

void EndProperties() {
    ImGui::EndTable();
}

void PropertyLabel(const char* label, const char* tooltip) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (tooltip) {
        ImGui::SetItemTooltip("%s", tooltip);
    }
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void Heading(const char* text, float scale) {
    ImGui::PushFont(GetBoldFont(), ImGui::GetStyle().FontSizeBase * scale);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void HelpMarker(const char* text) {
    ImGui::TextColored(EditorColors::TextMuted, "(?)");
    ImGui::SetItemTooltip("%s", text);
}

std::string EllipsizeStart(std::string_view text, float maxWidth) {
    const auto width = [](std::string_view part) {
        return ImGui::CalcTextSize(part.data(), part.data() + part.size()).x;
    };
    if (width(text) <= maxWidth) {
        return std::string(text);
    }
    constexpr std::string_view Ellipsis = "...";
    const float available = maxWidth - width(Ellipsis);
    // Drops whole characters from the start until the rest fits; UTF-8 continuation bytes are 10xxxxxx.
    std::size_t start = 0;
    while (start < text.size() && width(text.substr(start)) > available) {
        ++start;
        while (start < text.size() && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) {
            ++start;
        }
    }
    return std::string(Ellipsis) + std::string(text.substr(start));
}

std::string FormatRelativeTime(std::chrono::system_clock::time_point time, std::chrono::system_clock::time_point now) {
    using namespace std::chrono;
    const auto elapsed = now - time;
    if (time.time_since_epoch().count() == 0) {
        return "never";
    }
    if (elapsed < minutes(1)) {
        return "just now";
    }
    if (elapsed < hours(1)) {
        const auto count = duration_cast<minutes>(elapsed).count();
        return count == 1 ? "a minute ago" : std::format("{} minutes ago", count);
    }
    if (elapsed < hours(24)) {
        const auto count = duration_cast<hours>(elapsed).count();
        return count == 1 ? "an hour ago" : std::format("{} hours ago", count);
    }
    if (elapsed < hours(48)) {
        return "yesterday";
    }
    if (elapsed < days(30)) {
        return std::format("{} days ago", duration_cast<days>(elapsed).count());
    }
    return std::format("{:%Y-%m-%d}", floor<days>(time));
}

} // namespace Spot::Editor::Widgets
