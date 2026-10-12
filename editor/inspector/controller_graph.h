#pragma once

#include <imgui.h>
#include <spot.h>

#include <string>
#include <unordered_map>
#include <utility>

namespace Spot::Editor {

class EditorProject;

class ControllerGraph {
public:
    void Draw(AnimatorController& controller, EditorProject& project, int activeState = -1);
    static bool RemoveState(AnimatorController& controller, std::size_t index);
    const auto& GetRects() const { return m_rects; }

private:
    void Record(std::string name);
    int m_selected = 0;
    int m_transition = -1;
    int m_destination = 0;
    std::unordered_map<std::string, std::pair<ImVec2, ImVec2>> m_rects;
};

} // namespace Spot::Editor
