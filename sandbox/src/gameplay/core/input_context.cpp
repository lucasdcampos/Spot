#include "gameplay/core/input_context.h"

#include "gameplay/core/player_controller.h"

#include <algorithm>
#include <utility>

namespace Gameplay {

InputContext::InputContext(InputContextDesc desc) : m_desc(std::move(desc)) {
}

InputContext::InputContext(const InputContext& other) : m_desc(other.m_desc) {
}

InputContext::~InputContext() {
    Detach();
}

void InputContext::Activate(Controller* controller) {
    auto* player = dynamic_cast<PlayerController*>(controller);
    if (!player) {
        Deactivate();
        return;
    }
    if (player == m_controller) {
        // Closing only happens within a frame, so it reopens as if it had never closed.
        m_closing = false;
        return;
    }
    Detach();
    m_controller = player;
    player->m_contexts.push_back(this);
}

void InputContext::Deactivate() {
    if (m_controller) {
        m_closing = true;
    }
}

bool InputContext::IsActive() const {
    return m_controller && !m_closing;
}

void InputContext::SetActions(std::vector<std::string> actions) {
    m_desc.actions = std::move(actions);
}

bool InputContext::Takes(std::string_view action) const {
    return std::ranges::find(m_desc.actions, action) != m_desc.actions.end();
}

bool InputContext::IsAvailable(std::string_view action) const {
    return Sees(action, false);
}

void InputContext::Consume(std::string_view action) {
    if (m_controller && !m_controller->IsConsumed(action)) {
        m_controller->m_consumed.emplace_back(action);
    }
}

bool InputContext::IsDown(std::string_view action) const {
    return Sees(action, false) && Spot::Input::IsActionDown(action);
}

bool InputContext::IsPressed(std::string_view action) const {
    return Sees(action, true) && Spot::Input::IsActionPressed(action);
}

bool InputContext::IsReleased(std::string_view action) const {
    return Sees(action, true) && Spot::Input::IsActionReleased(action);
}

float InputContext::GetValue(std::string_view action) const {
    return Sees(action, false) ? Spot::Input::GetActionValue(action) : 0.0f;
}

Spot::Vector2 InputContext::GetVector2(std::string_view action) const {
    return Sees(action, false) ? Spot::Input::GetActionVector2(action) : Spot::Vector2{};
}

bool InputContext::Sees(std::string_view action, bool edge) const {
    if (!IsActive() || !m_controller->IsActiveAndEnabled() || !Takes(action)) {
        return false;
    }
    if (edge && m_controller->IsConsumed(action)) {
        return false;
    }
    return !m_controller->IsTakenAbove(*this, action);
}

void InputContext::Detach() {
    if (m_controller) {
        std::erase(m_controller->m_contexts, this);
    }
    m_controller = nullptr;
    m_closing = false;
}

} // namespace Gameplay
