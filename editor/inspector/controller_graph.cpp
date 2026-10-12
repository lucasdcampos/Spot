#include "inspector/controller_graph.h"

#include "imgui/widgets.h"
#include "inspector/inspector_backend.h"
#include "project/editor_project.h"

#include <algorithm>
#include <format>

namespace Spot::Editor {

bool ControllerGraph::RemoveState(AnimatorController& controller, std::size_t index) {
    if (controller.states.size() <= 1 || index >= controller.states.size()) {
        return false;
    }
    const int removed = static_cast<int>(index);
    controller.states.erase(controller.states.begin() + static_cast<std::ptrdiff_t>(index));
    std::erase_if(controller.transitions,
                  [&](const auto& transition) { return transition.from == removed || transition.to == removed; });
    for (auto& transition : controller.transitions) {
        if (transition.from > removed) {
            --transition.from;
        }
        if (transition.to > removed) {
            --transition.to;
        }
    }
    controller.entryState = controller.entryState == removed  ? 0
                            : controller.entryState > removed ? controller.entryState - 1
                                                              : controller.entryState;
    return true;
}

void ControllerGraph::Record(std::string name) {
    m_rects[std::move(name)] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
}

void ControllerGraph::Draw(AnimatorController& controller, EditorProject& project, int activeState) {
    auto& assets = project.GetAssets();
    m_rects.clear();
    if (m_selected >= static_cast<int>(controller.states.size())) {
        m_selected = 0;
    }
    if (m_transition >= static_cast<int>(controller.transitions.size())) {
        m_transition = -1;
    }
    if (m_destination >= static_cast<int>(controller.states.size())) {
        m_destination = 0;
    }
    const auto clips = assets.GetAssetIds(AssetType::AnimationClip);
    ImGui::BeginDisabled(clips.empty());
    if (ImGui::Button("Add state")) {
        int suffix = static_cast<int>(controller.states.size());
        std::string name;
        do {
            name = std::format("State {}", suffix++);
        } while (std::ranges::any_of(controller.states, [&](const auto& state) { return state.name == name; }));
        controller.states.push_back({.name = std::move(name), .clip = assets.Load<AnimationClip>(clips.front())});
        m_selected = static_cast<int>(controller.states.size() - 1);
    }
    Record("Controller/AddState");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_selected < 0 || controller.states.size() < 2);
    if (ImGui::Button("Remove state") && RemoveState(controller, static_cast<std::size_t>(m_selected))) {
        m_selected = 0;
        m_transition = -1;
    }
    Record("Controller/RemoveState");
    ImGui::EndDisabled();
    if (clips.empty()) {
        ImGui::TextDisabled("Import animation clips to add states.");
    }

    const auto position = [&](std::size_t i) {
        const auto authored = controller.states[i].editorPosition;
        return authored == Vector2{}
                   ? Vector2{20 + static_cast<float>(i % 3) * 166, 64 + static_cast<float>(i / 3) * 86}
                   : authored;
    };
    if (ImGui::BeginChild("##controller_graph", {0, 250}, ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::Dummy({560, std::max(220.0f, static_cast<float>((controller.states.size() + 2) / 3) * 86 + 64)});
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 any{origin.x + 70, origin.y + 20};
        for (const auto& transition : controller.transitions) {
            if (transition.to < 0 || static_cast<std::size_t>(transition.to) >= controller.states.size() ||
                transition.from >= static_cast<int>(controller.states.size())) {
                continue;
            }
            const auto from =
                transition.from >= 0 ? position(static_cast<std::size_t>(transition.from)) : Vector2{0, -20};
            const auto to = position(static_cast<std::size_t>(transition.to));
            const ImVec2 start = transition.from < 0 ? any : ImVec2{origin.x + from.x + 140, origin.y + from.y + 27};
            const ImVec2 end{origin.x + to.x, origin.y + to.y + 27};
            const ImU32 color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            draw->AddBezierCubic(start, {start.x + 30, start.y}, {end.x - 30, end.y}, end, color, 2);
            draw->AddTriangleFilled(end, {end.x - 7, end.y - 4}, {end.x - 7, end.y + 4}, color);
        }
        ImGui::SetCursorScreenPos({origin.x + 20, origin.y + 5});
        if (ImGui::Button("Any state", {100, 28})) {
            m_selected = -1;
        }
        Record("Controller/AnyState");
        for (std::size_t i = 0; i < controller.states.size(); ++i) {
            const auto p = position(i);
            const ImVec2 min{origin.x + p.x, origin.y + p.y}, max{min.x + 140, min.y + 54};
            const bool active = static_cast<int>(i) == activeState;
            draw->AddRectFilled(min, max, ImGui::GetColorU32(active ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg), 5);
            draw->AddRect(min, max,
                          ImGui::GetColorU32(static_cast<int>(i) == m_selected ? ImGuiCol_CheckMark : ImGuiCol_Border),
                          5.0f, 2.0f, 0);
            const auto& state = controller.states[i];
            draw->AddText({min.x + 8, min.y + 5}, ImGui::GetColorU32(ImGuiCol_Text), state.name.c_str());
            const char* kind = state.blendTree.type == BlendTreeType::OneDimensional  ? "1D blend"
                               : state.blendTree.type == BlendTreeType::Directional2D ? "2D directional blend"
                                                                                      : "Clip";
            draw->AddText({min.x + 8, min.y + 29}, ImGui::GetColorU32(ImGuiCol_TextDisabled), kind);
            if (static_cast<int>(i) == controller.entryState) {
                draw->AddCircleFilled({min.x + 130, min.y + 12}, 4, ImGui::GetColorU32(ImGuiCol_CheckMark));
            }
            ImGui::SetCursorScreenPos(min);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::InvisibleButton("##state", {140, 54})) {
                m_selected = static_cast<int>(i);
            }
            Record(std::format("Controller/State/{}", i));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const auto delta = ImGui::GetIO().MouseDelta;
                controller.states[i].editorPosition = {std::max(p.x + delta.x, 0.0f), std::max(p.y + delta.y, 36.0f)};
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    const auto stateCombo = [&](const char* label, int& selected, bool anyState) {
        const char* name = selected >= 0 && static_cast<std::size_t>(selected) < controller.states.size()
                               ? controller.states[static_cast<std::size_t>(selected)].name.c_str()
                               : "Any state";
        if (ImGui::BeginCombo(label, name)) {
            if (anyState && ImGui::Selectable("Any state", selected == -1)) {
                selected = -1;
            }
            for (std::size_t i = 0; i < controller.states.size(); ++i) {
                if (ImGui::Selectable(controller.states[i].name.c_str(), selected == static_cast<int>(i))) {
                    selected = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }
    };
    stateCombo("Entry", controller.entryState, false);
    stateCombo("Destination", m_destination, false);
    if (ImGui::Button("Add transition") && m_destination >= 0 &&
        static_cast<std::size_t>(m_destination) < controller.states.size()) {
        controller.transitions.push_back({.from = m_selected, .to = m_destination});
        m_transition = static_cast<int>(controller.transitions.size() - 1);
    }
    Record("Controller/AddTransition");
    for (std::size_t i = 0; i < controller.transitions.size(); ++i) {
        const auto& transition = controller.transitions[i];
        const std::string from =
            transition.from >= 0 && static_cast<std::size_t>(transition.from) < controller.states.size()
                ? controller.states[static_cast<std::size_t>(transition.from)].name
                : "Any state";
        const std::string to = transition.to >= 0 && static_cast<std::size_t>(transition.to) < controller.states.size()
                                   ? controller.states[static_cast<std::size_t>(transition.to)].name
                                   : "Missing state";
        if (ImGui::Selectable(std::format("{} -> {}##{}", from, to, i).c_str(), m_transition == static_cast<int>(i))) {
            m_transition = static_cast<int>(i);
        }
    }
    if (m_transition >= 0 && static_cast<std::size_t>(m_transition) < controller.transitions.size()) {
        auto& transition = controller.transitions[static_cast<std::size_t>(m_transition)];
        stateCombo("From", transition.from, true);
        stateCombo("To", transition.to, false);
        if (Widgets::BeginProperties("##transition")) {
            InspectorBackend backend({.project = &project});
            Archive archive(backend);
            archive.Field("conditions", transition.conditions);
            archive.Field("exitTime", transition.exitTime);
            archive.Field("duration", transition.duration, {.min = 0, .max = 10});
            archive.Field("interruptible", transition.interruptible);
            Widgets::EndProperties();
        }
        if (ImGui::Button("Remove transition")) {
            controller.transitions.erase(controller.transitions.begin() + m_transition);
            m_transition = -1;
        }
    }
    if (m_selected >= 0 && static_cast<std::size_t>(m_selected) < controller.states.size() &&
        Widgets::BeginProperties("##state_fields")) {
        InspectorBackend backend({.project = &project});
        Archive archive(backend);
        controller.states[static_cast<std::size_t>(m_selected)].Serialize(archive);
        Widgets::EndProperties();
    }
    if (Widgets::BeginProperties("##parameters")) {
        InspectorBackend backend({.project = &project});
        Archive archive(backend);
        archive.Field("parameters", controller.parameters);
        Widgets::EndProperties();
    }
}

} // namespace Spot::Editor
