#include "spot/scene/ui/ui_input_system.h"

#include "spot/core/debug/profiler.h"
#include "spot/platform/input.h"
#include "spot/scene/scene.h"
#include "spot/scene/ui/canvas.h"
#include "spot/scene/ui/ui_system.h"
#include "spot/scene/ui/ui_widgets.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Spot {

UiInputSystem::~UiInputSystem() {
    SetInputState(nullptr);
}
void UiInputSystem::SetInputState(InputState* input) {
    if (m_input == input)
        return;
    if (m_input && m_capture != InputCaptureId::None)
        m_input->PopCapture(m_capture);
    m_capture = InputCaptureId::None;
    m_input = input;
    UpdateCapture();
}
UiWidget* UiInputSystem::FindWidget(EntityId id) const {
    auto* entity = GetScene().GetEntity(id);
    if (!entity)
        return nullptr;
    UiWidget* result = nullptr;
    entity->ForEachComponent<UiWidget>([&](UiWidget& widget) {
        if (!result && widget.IsFocusable() && widget.IsInteractable() && widget.IsActiveAndEnabled())
            result = &widget;
    });
    return result;
}
bool UiInputSystem::WantsTextInput() const {
    const auto* widget = FindWidget(m_focused);
    return widget && widget->WantsTextInput();
}
bool UiInputSystem::IsTextInputCaptured(const InputState& input) {
    return input.GetCaptureOwner() == "Runtime UI text";
}
void UiInputSystem::UpdateCapture() {
    if (!m_input)
        return;
    const bool textCapture = WantsTextInput();
    const bool capture = m_modal != EntityId::Invalid || textCapture;
    if (!m_input)
        return;
    if (m_capture != InputCaptureId::None && m_textCapture != textCapture)
        m_input->PopCapture(std::exchange(m_capture, InputCaptureId::None));
    m_textCapture = textCapture;
    if (capture && m_capture == InputCaptureId::None)
        m_capture = m_input->PushCapture(textCapture ? "Runtime UI text" : "Runtime UI");
    else if (!capture && m_capture != InputCaptureId::None)
        m_input->PopCapture(std::exchange(m_capture, InputCaptureId::None));
}
void UiInputSystem::Refresh(Vector2 viewSize) {
    SPOT_PROFILE_SCOPE("UiInputSystem::Refresh");
    m_viewSize = viewSize;
    // A HUD of labels and images, with no menu open, takes no input: laying it out here every event and frame would
    // only cost time, so input waits until something can be focused, clicked or hold the input as a modal canvas.
    bool interactive = false;
    GetScene().ForEachComponent<Canvas>([&](Canvas& canvas) { interactive = interactive || canvas.settings.modal; });
    GetScene().ForEachComponent<UiWidget>(
        [&](UiWidget& widget) { interactive = interactive || (widget.IsFocusable() && widget.IsInteractable()); });
    if (!interactive) {
        m_modal = EntityId::Invalid;
        m_focusable.clear();
        m_focused = EntityId::Invalid;
        m_pressed = EntityId::Invalid;
        m_hovered = EntityId::Invalid;
        UpdateCapture();
        return;
    }
    auto& layout = GetScene().GetOrAddSystem<Spot::UiSystem>();
    layout.Layout(viewSize);
    m_modal = EntityId::Invalid;
    for (const auto& entry : layout.GetEntries()) {
        if (entry.entity == entry.canvas) {
            const auto* canvas = GetScene().GetEntity(entry.entity)->GetComponent<Canvas>();
            if (canvas->settings.modal)
                m_modal = entry.canvas;
        }
    }
    m_focusable.clear();
    for (const auto& entry : layout.GetEntries()) {
        if ((m_modal == EntityId::Invalid || entry.canvas == m_modal) && !entry.bounds.IsEmpty() &&
            FindWidget(entry.entity))
            m_focusable.push_back(entry.entity);
    }
    if (std::ranges::find(m_focusable, m_focused) == m_focusable.end())
        m_focused = EntityId::Invalid;
    if (std::ranges::find(m_focusable, m_pressed) == m_focusable.end())
        m_pressed = EntityId::Invalid;
    m_hovered = HitTest(m_pointer);
    UpdateStates();
    UpdateCapture();
}
EntityId UiInputSystem::HitTest(Vector2 point) const {
    const auto* layout = GetScene().GetSystem<UiSystem>();
    if (!layout)
        return EntityId::Invalid;
    const auto entries = layout->GetEntries();
    for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
        if ((m_modal == EntityId::Invalid || entry->canvas == m_modal) && entry->bounds.Contains(point) &&
            entry->clip.Contains(point) && FindWidget(entry->entity))
            return entry->entity;
    }
    return EntityId::Invalid;
}
void UiInputSystem::UpdateStates() {
    GetScene().ForEachComponent<UiWidget>([&](UiWidget& widget) {
        const auto id = widget.GetEntity().GetId();
        widget.SetUiState(id == m_pressed   ? UiControlState::Pressed
                          : id == m_focused ? UiControlState::Focused
                          : id == m_hovered ? UiControlState::Hovered
                                            : UiControlState::Normal);
    });
}
void UiInputSystem::Send(EntityId entity, const UiEvent& event) {
    if (auto* widget = FindWidget(entity))
        widget->HandleUiEvent(event);
}
bool UiInputSystem::Focus(EntityId id) {
    if (std::ranges::find(m_focusable, id) == m_focusable.end())
        return false;
    m_focused = id;
    auto* entity = GetScene().GetEntity(id);
    const auto* system = GetScene().GetSystem<UiSystem>();
    for (auto* parent = entity ? entity->GetParent() : nullptr; parent; parent = parent->GetParent()) {
        auto* scroll = parent->GetComponent<UiScrollView>();
        if (!scroll || !scroll->IsActiveAndEnabled())
            continue;
        const auto* item = system->Find(id);
        const auto* viewport = system->Find(parent->GetId());
        if (!item || !viewport)
            continue;
        Vector2 offset = scroll->GetOffset();
        if (item->bounds.min.x < viewport->bounds.min.x)
            offset.x -= (viewport->bounds.min.x - item->bounds.min.x) / viewport->scale;
        else if (item->bounds.max.x > viewport->bounds.max.x)
            offset.x += (item->bounds.max.x - viewport->bounds.max.x) / viewport->scale;
        if (item->bounds.min.y < viewport->bounds.min.y)
            offset.y += (viewport->bounds.min.y - item->bounds.min.y) / viewport->scale;
        else if (item->bounds.max.y > viewport->bounds.max.y)
            offset.y -= (item->bounds.max.y - viewport->bounds.max.y) / viewport->scale;
        scroll->SetOffset(offset);
        GetScene().GetSystem<UiSystem>()->Layout(m_viewSize);
    }
    UpdateStates();
    UpdateCapture();
    return true;
}
void UiInputSystem::ClearFocus() {
    m_focused = EntityId::Invalid;
    UpdateStates();
    UpdateCapture();
}
void UiInputSystem::Tab(bool reverse) {
    if (m_focusable.empty())
        return;
    const auto found = std::ranges::find(m_focusable, m_focused);
    std::size_t index =
        found == m_focusable.end()
            ? (reverse ? m_focusable.size() - 1 : 0)
            : (static_cast<std::size_t>(found - m_focusable.begin()) + (reverse ? m_focusable.size() - 1 : 1)) %
                  m_focusable.size();
    Focus(m_focusable[index]);
}
void UiInputSystem::Navigate(Vector2 direction) {
    if (m_focused == EntityId::Invalid) {
        Tab(false);
        return;
    }
    const auto* layout = GetScene().GetSystem<UiSystem>();
    const auto* from = layout->Find(m_focused);
    if (!from)
        return;
    float bestScore = std::numeric_limits<float>::max();
    EntityId next = EntityId::Invalid;
    for (const auto id : m_focusable) {
        if (id == m_focused)
            continue;
        const Vector2 difference = layout->Find(id)->bounds.GetCenter() - from->bounds.GetCenter();
        const float forward = difference.x * direction.x + difference.y * direction.y;
        const float perpendicular = std::abs(difference.x * direction.y - difference.y * direction.x);
        const float score = forward + perpendicular * 4;
        if (forward > 0.01f && score < bestScore) {
            next = id;
            bestScore = score;
        }
    }
    if (next != EntityId::Invalid)
        Focus(next);
}
bool UiInputSystem::OnEvent(const Event& event, Vector2 viewSize) {
    bool ownedRelease = false;
    if (const auto* key = event.GetIf<KeyReleasedEvent>()) {
        const auto index = static_cast<std::size_t>(key->key);
        ownedRelease = m_ownedKeys.test(index);
        m_ownedKeys.reset(index);
    }
    if (const auto* mouse = event.GetIf<MouseButtonReleasedEvent>(); mouse && mouse->button == MouseButton::Left)
        ownedRelease = std::exchange(m_ownedLeft, false);
    if (const auto* pad = event.GetIf<GamepadButtonReleasedEvent>()) {
        const auto owned = std::pair{pad->gamepad, pad->button};
        const auto found = std::ranges::find(m_ownedPads, owned);
        if (found != m_ownedPads.end()) {
            ownedRelease = true;
            m_ownedPads.erase(found);
        }
    }
    if (IsFaulted())
        return ownedRelease;
    const Detail::DeferRemovalsScope defer(GetScene());
    bool consumed = ownedRelease;
    InvokeHook("UiInput", [&] {
        Refresh(viewSize);
        bool rawConsumed = false;
        OnRawEvent.Emit(event, rawConsumed);
        consumed = rawConsumed || HandleEvent(event) || ownedRelease;
        Refresh(viewSize);
    });
    if (consumed) {
        if (const auto* key = event.GetIf<KeyPressedEvent>())
            m_ownedKeys.set(static_cast<std::size_t>(key->key));
        if (const auto* mouse = event.GetIf<MouseButtonPressedEvent>(); mouse && mouse->button == MouseButton::Left)
            m_ownedLeft = true;
        if (const auto* pad = event.GetIf<GamepadButtonPressedEvent>()) {
            const auto owned = std::pair{pad->gamepad, pad->button};
            if (std::ranges::find(m_ownedPads, owned) == m_ownedPads.end())
                m_ownedPads.push_back(owned);
        }
    }
    if (IsFaulted()) {
        m_modal = m_focused = m_pressed = EntityId::Invalid;
        UpdateCapture();
        return true;
    }
    return consumed;
}
bool UiInputSystem::HandleEvent(const Event& event) {
    const bool modal = m_modal != EntityId::Invalid;
    if (const auto* focus = event.GetIf<WindowFocusEvent>(); focus && !focus->focused) {
        m_pressed = m_hovered = EntityId::Invalid;
        m_axisDirections = {};
        ClearFocus();
        return false;
    }
    if (event.Is<MouseLeaveEvent>()) {
        m_pointer = {-1, -1};
        m_hovered = EntityId::Invalid;
        UpdateStates();
        return false;
    }
    if (const auto* moved = event.GetIf<MouseMovedEvent>()) {
        m_pointer = {moved->position.x, m_viewSize.y - moved->position.y};
        m_hovered = HitTest(m_pointer);
        if (m_pressed != EntityId::Invalid)
            Send(m_pressed, {.type = UiEventType::PointerMove, .position = m_pointer});
        UpdateStates();
        return modal || m_pressed != EntityId::Invalid;
    }
    if (const auto* pressed = event.GetIf<MouseButtonPressedEvent>(); pressed && pressed->button == MouseButton::Left) {
        m_pressed = HitTest(m_pointer);
        if (m_pressed != EntityId::Invalid) {
            Focus(m_pressed);
            UpdateStates();
            Send(m_pressed, {.type = UiEventType::PointerDown, .position = m_pointer});
        } else
            ClearFocus();
        return modal || m_pressed != EntityId::Invalid;
    }
    if (const auto* released = event.GetIf<MouseButtonReleasedEvent>();
        released && released->button == MouseButton::Left) {
        const auto pressed = std::exchange(m_pressed, EntityId::Invalid);
        if (pressed != EntityId::Invalid) {
            Send(pressed, {.type = UiEventType::PointerUp, .position = m_pointer});
            if (pressed == HitTest(m_pointer))
                Send(pressed, {.type = UiEventType::Activate, .position = m_pointer});
        }
        UpdateStates();
        return modal || pressed != EntityId::Invalid;
    }
    if (const auto* wheel = event.GetIf<MouseScrolledEvent>()) {
        const auto* layout = GetScene().GetSystem<UiSystem>();
        for (auto entry = layout->GetEntries().rbegin(); entry != layout->GetEntries().rend(); ++entry) {
            if ((modal && entry->canvas != m_modal) || !entry->bounds.Contains(m_pointer) ||
                !entry->clip.Contains(m_pointer))
                continue;
            auto* entity = GetScene().GetEntity(entry->entity);
            if (auto* scroll = entity->GetComponent<UiScrollView>();
                scroll && scroll->IsActiveAndEnabled() && scroll->IsInteractable()) {
                scroll->HandleUiEvent({.type = UiEventType::Scroll, .position = wheel->delta, .value = wheel->delta.y});
                return true;
            }
        }
        return modal;
    }
    if (const auto* typed = event.GetIf<TextInputEvent>()) {
        if (WantsTextInput()) {
            Send(m_focused, {.type = UiEventType::Text, .text = typed->text});
            return true;
        }
        return modal;
    }
    if (const auto* key = event.GetIf<KeyReleasedEvent>()) {
        if (key->key == Key::LeftShift || key->key == Key::RightShift)
            m_shift = false;
        return modal || WantsTextInput();
    }
    if (const auto* key = event.GetIf<KeyPressedEvent>()) {
        if (key->key == Key::LeftShift || key->key == Key::RightShift) {
            m_shift = true;
            return modal || WantsTextInput();
        }
        Key dismiss = Key::Escape;
        if (modal)
            dismiss = GetScene().GetEntity(m_modal)->GetComponent<Canvas>()->settings.dismissKey;
        // A letter that dismisses the canvas is typed into a focused field instead.
        if (key->key == Key::Escape || (modal && key->key == dismiss && !WantsTextInput())) {
            const bool hadFocus = m_focused != EntityId::Invalid;
            if (WantsTextInput() && key->key == Key::Escape)
                ClearFocus();
            else if (modal)
                OnCancel.Emit(m_modal);
            else
                ClearFocus();
            return modal || hadFocus;
        }
        if (key->key == Key::Tab) {
            Tab(m_shift);
            return !m_focusable.empty();
        }
        if (WantsTextInput()) {
            switch (key->key) {
            case Key::Backspace:
                Send(m_focused, {.type = UiEventType::Backspace});
                break;
            case Key::Delete:
                Send(m_focused, {.type = UiEventType::Delete});
                break;
            case Key::Left:
                Send(m_focused, {.type = UiEventType::Left});
                break;
            case Key::Right:
                Send(m_focused, {.type = UiEventType::Right});
                break;
            case Key::Home:
                Send(m_focused, {.type = UiEventType::Home});
                break;
            case Key::End:
                Send(m_focused, {.type = UiEventType::End});
                break;
            case Key::Enter:
                Send(m_focused, {.type = UiEventType::Submit});
                break;
            default:
                break;
            }
            return true;
        }
        if (key->key == Key::Enter || key->key == Key::Space) {
            if (m_focused == EntityId::Invalid)
                Tab(false);
            Send(m_focused, {.type = UiEventType::Activate});
            return m_focused != EntityId::Invalid || modal;
        }
        Vector2 direction;
        if (key->key == Key::Left)
            direction = {-1, 0};
        if (key->key == Key::Right)
            direction = {1, 0};
        if (key->key == Key::Up)
            direction = {0, 1};
        if (key->key == Key::Down)
            direction = {0, -1};
        if (direction != Vector2{}) {
            auto* widget = FindWidget(m_focused);
            if (widget && (dynamic_cast<UiSlider*>(widget) || dynamic_cast<UiScrollView*>(widget)))
                Send(m_focused,
                     {.type = UiEventType::Adjust, .position = direction, .value = direction.x + direction.y});
            else
                Navigate(direction);
            return !m_focusable.empty();
        }
        return modal;
    }
    if (const auto* pad = event.GetIf<GamepadButtonPressedEvent>()) {
        if (pad->button == GamepadButton::East || pad->button == GamepadButton::Start) {
            if (modal)
                OnCancel.Emit(m_modal);
            else
                ClearFocus();
            return modal;
        }
        if (pad->button == GamepadButton::South) {
            if (m_focused == EntityId::Invalid)
                Tab(false);
            Send(m_focused, {.type = UiEventType::Activate});
            return !m_focusable.empty();
        }
        if (pad->button == GamepadButton::LeftShoulder || pad->button == GamepadButton::RightShoulder) {
            Tab(pad->button == GamepadButton::LeftShoulder);
            return !m_focusable.empty();
        }
        Vector2 direction;
        if (pad->button == GamepadButton::DPadLeft)
            direction = {-1, 0};
        if (pad->button == GamepadButton::DPadRight)
            direction = {1, 0};
        if (pad->button == GamepadButton::DPadUp)
            direction = {0, 1};
        if (pad->button == GamepadButton::DPadDown)
            direction = {0, -1};
        if (direction != Vector2{}) {
            if (auto* widget = FindWidget(m_focused);
                widget && (dynamic_cast<UiSlider*>(widget) || dynamic_cast<UiScrollView*>(widget)))
                Send(m_focused,
                     {.type = UiEventType::Adjust, .position = direction, .value = direction.x + direction.y});
            else
                Navigate(direction);
            return !m_focusable.empty();
        }
        return modal;
    }
    if (const auto* axis = event.GetIf<GamepadAxisEvent>();
        axis && (axis->axis == GamepadAxis::LeftX || axis->axis == GamepadAxis::LeftY)) {
        const auto index = axis->axis == GamepadAxis::LeftX ? 0u : 1u;
        const int direction = axis->value > 0.5f ? 1 : axis->value < -0.5f ? -1 : 0;
        if (direction && direction != m_axisDirections[index])
            Navigate(index == 0 ? Vector2{static_cast<float>(direction), 0}
                                : Vector2{0, static_cast<float>(direction)});
        m_axisDirections[index] = direction;
        return modal || !m_focusable.empty();
    }
    return modal &&
           (event.Is<GamepadButtonReleasedEvent>() || event.Is<TextCompositionEvent>() || event.Is<GamepadAxisEvent>());
}
void UiInputSystem::OnBeforeFixedSteps(float) {
    if (!m_input)
        SetInputState(Input::GetState());
    Refresh(m_viewSize);
}

} // namespace Spot

namespace Spot {
bool UiInputSystem::HandleUiEvent(const Event& event, Vector2 viewSize, InputState* input) {
    if (!GetScene().FindComponent<Canvas>())
        return false;
    SetInputState(input);
    const bool consumed = OnEvent(event, viewSize);
    if (consumed && input)
        input->ConsumeEvent(event);
    return consumed;
}
} // namespace Spot
