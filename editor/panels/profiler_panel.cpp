#include "panels/profiler_panel.h"

#include <spot.h>
#include <spot/scene/scripting/script_runtime.h>

#include <algorithm>
#include <format>
#include <map>
#include <string>

namespace Spot::Editor {
namespace {
double Milliseconds(std::chrono::nanoseconds time) {
    return std::chrono::duration<double, std::milli>(time).count();
}
} // namespace

ProfilerPanel::ProfilerPanel(RemoteFrames remoteFrames)
    : Panel("Profiler", "Profiler"), m_remoteFrames(std::move(remoteFrames)) {
}

void ProfilerPanel::OnDraw() {
    bool paused = m_model.IsPaused();
    if (ImGui::Checkbox("Pause", &paused)) {
        m_model.SetPaused(paused);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Game process", &m_remote)) {
        m_model.Clear();
        m_selected = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        m_model.Clear();
        m_selected = 0;
    }
    ImGui::SameLine();
    bool scripts = IsScriptProfiling();
    if (ImGui::Checkbox("Script functions", &scripts))
        SetScriptProfiling(scripts);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Profiles each function of the scripts running in the editor; it slows scripts down");
    const auto frames =
        m_remote ? (m_remoteFrames ? m_remoteFrames() : std::vector<const ProfileFrame*>{}) : Profiler::GetFrames();
    m_model.Update(frames);
    if (scripts)
        DrawScripts();
    if (m_model.GetFrames().empty()) {
        ImGui::TextDisabled("No recorded frames");
        return;
    }
    DrawFrames();
    const auto& recorded = m_model.GetFrames();
    auto selected = std::ranges::find(recorded, m_selected, &ProfileFrame::number);
    DrawTimeline(selected == recorded.end() ? recorded.back() : *selected);
    DrawAverages();
}

void ProfilerPanel::DrawFrames() {
    const auto& frames = m_model.GetFrames();
    double peak = 1.0, total = 0.0;
    for (const auto& frame : frames) {
        peak = std::max(peak, Milliseconds(frame.duration));
        total += Milliseconds(frame.duration);
    }
    ImGui::Text("%zu frames | average %.2f ms | peak %.2f ms", frames.size(),
                total / static_cast<double>(frames.size()), peak);
    const auto start = ImGui::GetCursorScreenPos();
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    constexpr float Height = 80.0f;
    ImGui::InvisibleButton("##Frames", {width, Height});
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, {start.x + width, start.y + Height}, IM_COL32(25, 27, 32, 255));
    const float column = width / static_cast<float>(frames.size());
    std::size_t index = 0;
    for (const auto& frame : frames) {
        const float left = start.x + static_cast<float>(index++) * column;
        const float height = static_cast<float>(Milliseconds(frame.duration) / peak) * Height;
        draw->AddRectFilled({left, start.y + Height - height}, {left + std::max(1.0f, column - 1.0f), start.y + Height},
                            frame.number == m_selected ? IM_COL32(255, 190, 75, 255) : IM_COL32(65, 155, 225, 255));
    }
    if (ImGui::IsItemHovered()) {
        const auto hovered = std::min(
            frames.size() - 1, static_cast<std::size_t>(std::max(0.0f, ImGui::GetIO().MousePos.x - start.x) / column));
        ImGui::SetTooltip("Frame %llu: %.3f ms\nClick to select and pause",
                          static_cast<unsigned long long>(frames[hovered].number),
                          Milliseconds(frames[hovered].duration));
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_selected = frames[hovered].number;
            m_model.SetPaused(true);
        }
    }
}

void ProfilerPanel::DrawTimeline(const ProfileFrame& frame) {
    ImGui::Text("Frame %llu | %.3f ms | %zu scopes | %zu dropped", static_cast<unsigned long long>(frame.number),
                Milliseconds(frame.duration), frame.samples.size(), frame.droppedSamples);
    const auto start = ImGui::GetCursorScreenPos();
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    constexpr float Row = 20.0f;
    // A lane per thread, the frame's own first, each as deep as its deepest scope.
    std::map<std::uint32_t, std::uint32_t> laneDepths;
    for (const auto& sample : frame.samples) {
        auto& depth = laneDepths[sample.thread];
        depth = std::max(depth, std::min(sample.depth, 15u) + 1);
    }
    std::map<std::uint32_t, std::uint32_t> laneRows;
    std::uint32_t rows = 0;
    for (const auto& [thread, depth] : laneDepths) {
        laneRows[thread] = rows;
        rows += depth;
    }
    ImGui::InvisibleButton("##Timeline", {width, Row * static_cast<float>(std::max(rows, 1u))});
    auto* draw = ImGui::GetWindowDrawList();
    const double duration = static_cast<double>(std::max<std::int64_t>(1, frame.duration.count()));
    for (const auto& sample : frame.samples) {
        const float left =
            start.x +
            static_cast<float>(std::clamp(static_cast<double>(sample.start.count()) / duration, 0.0, 1.0)) * width;
        const float right =
            start.x +
            static_cast<float>(std::clamp(
                (static_cast<double>(sample.start.count()) + static_cast<double>(sample.duration.count())) / duration,
                0.0, 1.0)) *
                width;
        const float top = start.y + Row * static_cast<float>(laneRows[sample.thread] + std::min(sample.depth, 15u));
        const ImVec2 end{std::min(start.x + width, std::max(left + 1.0f, right)), top + Row - 2.0f};
        draw->AddRectFilled({left, top}, end, IM_COL32(45 + (sample.depth % 4) * 20, 100, 165, 255), 2.0f);
        if (end.x > left + 4.0f) {
            draw->PushClipRect({left + 2.0f, top}, end, true);
            draw->AddText({left + 3.0f, top}, IM_COL32(240, 240, 240, 255), sample.name.data(),
                          sample.name.data() + sample.name.size());
            draw->PopClipRect();
        }
        if (ImGui::IsMouseHoveringRect({left, top}, end)) {
            const std::string thread =
                sample.thread == 0 ? std::string("frame thread") : std::format("thread {}", sample.thread);
            ImGui::SetTooltip("%.*s\n%.3f ms (inclusive), %s", static_cast<int>(sample.name.size()), sample.name.data(),
                              Milliseconds(sample.duration), thread.c_str());
        }
    }
}

void ProfilerPanel::DrawScripts() {
    ImGui::SeparatorText("Script functions (own time, since enabled)");
    if (ImGui::SmallButton("Reset"))
        ResetScriptProfile();
    if (m_remote) {
        ImGui::SameLine();
        ImGui::TextDisabled("Scripts of the game process are not listed here");
    }
    const auto profile = GetScriptProfile();
    if (profile.empty()) {
        ImGui::TextDisabled("No script functions ran yet");
        return;
    }
    const float height =
        std::min(ImGui::GetTextLineHeightWithSpacing() * 9.0f, ImGui::GetContentRegionAvail().y * 0.4f);
    if (!ImGui::BeginTable("##Scripts", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                           {0.0f, height})) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Function");
    ImGui::TableSetupColumn("Source");
    ImGui::TableSetupColumn("ms");
    ImGui::TableSetupColumn("Calls");
    ImGui::TableSetupColumn("Instructions");
    ImGui::TableSetupColumn("Allocations");
    ImGui::TableHeadersRow();
    for (const auto& function : profile) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(function.name.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(std::format("{}:{}", function.sourceName, function.line).c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", Milliseconds(function.time));
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(function.calls));
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(function.instructions));
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(function.allocations));
    }
    ImGui::EndTable();
}

void ProfilerPanel::DrawAverages() {
    ImGui::SeparatorText("Scope averages (inclusive, per recorded frame)");
    if (!ImGui::BeginTable("##Averages", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, {0.0f, 0.0f})) {
        return;
    }
    ImGui::TableSetupColumn("Scope");
    ImGui::TableSetupColumn("ms/frame");
    ImGui::TableSetupColumn("calls/frame");
    ImGui::TableHeadersRow();
    const auto averages = m_model.GetAverages();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(averages.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& average = averages[static_cast<std::size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(average.name.data(), average.name.data() + average.name.size());
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", average.milliseconds);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", average.callsPerFrame);
        }
    }
    ImGui::EndTable();
}

} // namespace Spot::Editor
