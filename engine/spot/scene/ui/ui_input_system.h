#pragma once

#include "spot/core/signal.h"
#include "spot/events/event.h"
#include "spot/input/input_state.h"
#include "spot/scene/entity.h"
#include "spot/scene/scene_system.h"

#include <array>
#include <bitset>
#include <vector>

namespace Spot {

class UiWidget;

class UiInputSystem final : public SceneSystem {
public:
    ~UiInputSystem() override;
    // The state must outlive this binding; detach before destroying a state (as editor play sessions do).
    void SetInputState(InputState* input);
    bool OnEvent(const Event& event, Vector2 viewSize);
    void Refresh(Vector2 viewSize);
    bool Focus(EntityId entity);
    void ClearFocus();
    EntityId GetFocusedEntity() const { return m_focused; }
    EntityId GetModalCanvas() const { return m_modal; }
    bool WantsTextInput() const;
    static bool IsTextInputCaptured(const InputState& input);
    Signal<EntityId> OnCancel;
    Signal<const Event&, bool&> OnRawEvent;

protected:
    void OnBeforeFixedSteps(float deltaTime) override;

private:
    UiWidget* FindWidget(EntityId entity) const;
    EntityId HitTest(Vector2 point) const;
    bool HandleEvent(const Event& event);
    void Navigate(Vector2 direction);
    void Tab(bool reverse);
    void UpdateStates();
    void UpdateCapture();
    void Send(EntityId entity, const struct UiEvent& event);
    InputState* m_input = nullptr;
    bool m_textCapture = false;
    InputCaptureId m_capture = InputCaptureId::None;
    EntityId m_focused = EntityId::Invalid;
    EntityId m_hovered = EntityId::Invalid;
    EntityId m_pressed = EntityId::Invalid;
    EntityId m_modal = EntityId::Invalid;
    Vector2 m_viewSize;
    Vector2 m_pointer{-1, -1};
    std::bitset<256> m_ownedKeys;
    bool m_ownedLeft = false;
    std::vector<std::pair<GamepadId, GamepadButton>> m_ownedPads;
    bool m_shift = false;
    std::array<int, 2> m_axisDirections = {};
    std::vector<EntityId> m_focusable;
};

} // namespace Spot
