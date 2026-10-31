#include "gameplay/menus/menu.h"
#include "gameplay/core/world_time.h"
#include <spot/scene/ui/ui_input_system.h>
#include <spot/scene/ui/ui_system.h>

#include <algorithm>
#include <cmath>

namespace Gameplay {

namespace {

// Menus ease in over this many seconds, rising this many canvas pixels into place as their backdrop darkens.
constexpr float OpenDuration = 0.16f;
constexpr float OpenRise = 10.0f;
// Controls change state in about 120 milliseconds: three of these.
constexpr float StateTime = 0.04f;

void Approach(Spot::Color& color, Spot::Color target, float blend) {
    color = {std::lerp(color.r, target.r, blend), std::lerp(color.g, target.g, blend),
             std::lerp(color.b, target.b, blend), std::lerp(color.a, target.a, blend)};
}

} // namespace

void MenuStyle::Serialize(Spot::Archive& archive) {
    archive.Field("accent", accent, {.tooltip = "What is selected"});
    archive.Field("text", text);
    archive.Field("textSecondary", textSecondary);
    archive.Field("textMuted", textMuted);
    archive.Field("line", line, {.tooltip = "Outlines at rest"});
    archive.Field("lineHovered", lineHovered);
    archive.Field("lineFocused", lineFocused);
}

MenuSystem& MenuSystem::Get(Spot::Scene& scene) {
    return scene.GetOrAddSystem<MenuSystem>();
}

Menu* Menu::GetOpen(Spot::Scene& scene) {
    Menu* open = nullptr;
    scene.ForEachComponent<Menu>([&](Menu& menu) {
        if (!open && menu.IsOpen()) {
            open = &menu;
        }
    });
    return open;
}

void Menu::CloseAll(Spot::Scene& scene) {
    scene.ForEachComponent<Menu>([](Menu& menu) { menu.Close(); });
}

void Menu::EnsureInitialized() {
    if (m_initialized) {
        return;
    }
    m_initialized = true;
    if (Spot::Entity* entity = GetScene().GetEntity(canvas)) {
        entity->SetActive(false);
    }
    auto& input = GetScene().GetOrAddSystem<Spot::UiInputSystem>();
    input.SetInputState(Spot::Input::GetState());
    m_connections.push_back(input.OnCancel.Connect([this](Spot::EntityId id) {
        if (m_open && id == canvas) {
            Close();
        }
    }));
    m_connections.push_back(input.OnRawEvent.Connect(
        [this](const Spot::Event& event, bool& consumed) { HandleRawEvent(event, consumed); }));
    Initialize();
    for (MenuPart* part : GetParts()) {
        part->OnMenuInitialize(*this);
    }
}

std::vector<MenuPart*> Menu::GetParts() const {
    std::vector<MenuPart*> parts;
    for (Spot::Component* component : GetEntity().GetComponents<Spot::Component>()) {
        if (auto* part = dynamic_cast<MenuPart*>(component)) {
            parts.push_back(part);
        }
    }
    return parts;
}

bool Menu::CanToggle() const {
    const auto parts = GetParts();
    return std::ranges::none_of(parts, [](const MenuPart* part) { return part->IsBusy(); });
}

void Menu::OnStart() {
    EnsureInitialized();
}

void Menu::Open() {
    EnsureInitialized();
    if (m_open) {
        return;
    }
    CloseAll(GetScene());
    MenuSystem::Get(GetScene()).OnOpening.Emit(*this);
    m_open = true;
    if (Spot::Entity* entity = GetScene().GetEntity(canvas)) {
        entity->SetActive(true);
        if (!m_measured) {
            m_measured = true;
            entity->ForEachChild([&](Spot::Entity& child) {
                if (const auto* rect = child.GetComponent<Spot::UiRect>()) {
                    m_offsets.emplace_back(child.GetId(), rect->layout.offset);
                }
            });
            if (const auto* backdrop = entity->GetComponent<Spot::UiImage>()) {
                m_backdrop = backdrop->tint.a;
            }
        }
    }
    m_time = 0.0f;
    Animate(0.0f);
    // Time is everyone's in a session, so menus pause it only alone.
    WorldTime::Get(GetScene());
    if (pausesGame && WorldTime::IsScalingAllowed(GetScene())) {
        m_previousTimeScale = Spot::Time::GetTimeScale();
        m_paused = true;
        Spot::Time::SetTimeScale(0.0f);
    }
    RefreshInput();
    Spot::Input::UpdateActions();
    GetScene().GetOrAddSystem<Spot::UiInputSystem>().Focus(firstFocus);
    OnOpen();
    for (MenuPart* part : GetParts()) {
        part->OnMenuOpen(*this);
    }
    PlayClick();
}

void Menu::Close() {
    if (Spot::Entity* entity = GetScene().GetEntity(canvas)) {
        entity->SetActive(false);
    }
    if (!m_open) {
        return;
    }
    m_open = false;
    if (m_paused) {
        Spot::Time::SetTimeScale(WorldTime::IsScalingAllowed(GetScene()) ? m_previousTimeScale : 1.0f);
        m_paused = false;
    }
    if (auto* input = GetScene().GetSystem<Spot::UiInputSystem>()) {
        input->ClearFocus();
        RefreshInput();
    }
    OnClose();
    for (MenuPart* part : GetParts()) {
        part->OnMenuClose(*this);
    }
}

void Menu::PlayClick() const {
    Spot::AudioDevice* device = Spot::Audio::GetDevice();
    if (device && clickSound) {
        device->Play(clickSound.GetShared(), {.volume = 0.5f, .bus = Spot::Audio::FindBus("Interface")});
    }
}

void Menu::RefreshInput() {
    auto& input = GetScene().GetOrAddSystem<Spot::UiInputSystem>();
    input.SetInputState(Spot::Input::GetState());
    auto size = GetScene().GetOrAddSystem<Spot::UiSystem>().GetViewSize();
    if (size.x <= 0 || size.y <= 0) {
        size = {1280, 720};
    }
    input.Refresh(size);
}

void Menu::ConnectButton(Spot::EntityId id, std::function<void()> callback) {
    if (auto* button = Get<Spot::UiButton>(id)) {
        m_connections.push_back(button->OnClick.Connect(std::move(callback)));
    }
}

void Menu::OnUpdate(float deltaTime) {
    if (!m_open && !toggleAction.empty() && CanToggle() && Spot::Input::IsActionPressed(toggleAction) &&
        !GetOpen(GetScene())) {
        Open();
    }
    if (m_open) {
        // Menus pause the game, so they keep time unscaled.
        const float elapsed = Spot::Time::GetUnscaledDeltaTime() > 0 ? Spot::Time::GetUnscaledDeltaTime() : deltaTime;
        m_time += elapsed;
        Animate(elapsed);
    }
}

void Menu::Animate(float elapsed) {
    const float t = std::clamp(m_time / OpenDuration, 0.0f, 1.0f);
    const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    for (const auto& [id, offset] : m_offsets) {
        if (auto* rect = Get<Spot::UiRect>(id)) {
            rect->layout.offset = offset - Spot::Vector2{0.0f, OpenRise * (1.0f - eased)};
        }
    }
    if (auto* backdrop = Get<Spot::UiImage>(canvas)) {
        backdrop->tint.a = m_backdrop * eased;
    }
    const float blend = elapsed > 0 ? 1.0f - std::exp(-elapsed / StateTime) : 0.0f;
    AnimateControls(blend);
    for (MenuPart* part : GetParts()) {
        part->AnimateMenu(*this, blend);
    }
}

void Menu::StyleControl(Spot::EntityId control, bool selected, Spot::Color rest, float blend) {
    const auto* widget = Get<Spot::UiWidget>(control);
    if (!widget || !widget->GetEntity().IsActiveInHierarchy()) {
        return;
    }
    // The selected control is outlined in the accent, the focused one in white, the one under the pointer faintly.
    const auto state = widget->GetUiState();
    const bool hot = state == Spot::UiControlState::Hovered || state == Spot::UiControlState::Pressed;
    const bool focused = state == Spot::UiControlState::Focused;
    if (auto* outline = GetChild<Spot::UiImage>(control, "Outline")) {
        Approach(outline->tint,
                 selected  ? style.accent
                 : focused ? style.lineFocused
                 : hot     ? style.lineHovered
                           : style.line,
                 blend);
    }
    if (auto* label = GetChild<Spot::UiText>(control, "Label")) {
        Approach(label->color, selected || hot || focused ? style.text : rest, blend);
    }
    if (auto* icon = GetChild<Spot::UiImage>(control, "Icon")) {
        Approach(icon->tint, selected ? style.accent : hot || focused ? style.text : style.textSecondary, blend);
    }
}

bool Menu::Matches(const Spot::Event& event) const {
    const Spot::InputActionMap* actions = Spot::Input::GetActionMap();
    const Spot::InputAction* action = actions && !toggleAction.empty() ? actions->Find(toggleAction) : nullptr;
    if (!action) {
        return false;
    }
    for (const auto& binding : action->bindings) {
        if (const auto* key = event.GetIf<Spot::KeyPressedEvent>();
            key && !key->repeat && binding.control == Spot::InputControl::Of(key->key)) {
            return true;
        }
        if (const auto* pad = event.GetIf<Spot::GamepadButtonPressedEvent>();
            pad && binding.control == Spot::InputControl::Of(pad->button)) {
            return true;
        }
    }
    return false;
}

void Menu::HandleRawEvent(const Spot::Event& event, bool& consumed) {
    if (consumed) {
        return;
    }
    if (m_open) {
        OnRawEvent(event, consumed);
        for (MenuPart* part : GetParts()) {
            if (!consumed) {
                part->OnMenuRawEvent(*this, event, consumed);
            }
        }
    }
    if (consumed || !CanToggle() || !Matches(event)) {
        return;
    }
    if (!m_open && !GetOpen(GetScene())) {
        Open();
        consumed = true;
    } else if (m_open && !GetScene().GetOrAddSystem<Spot::UiInputSystem>().WantsTextInput()) {
        Close();
        consumed = true;
    }
}

void Menu::OnDisable() {
    Close();
}

void Menu::OnDestroy() {
    Close();
    m_connections.clear();
}

void Menu::Serialize(Spot::Archive& archive) {
    archive.Field("canvas", canvas, {.tooltip = "Shown while it is open"});
    archive.Field("toggleAction", toggleAction, {.tooltip = "The input action that opens and closes it"});
    archive.Field("playerIndex", playerIndex, {.min = 0, .tooltip = "The local player this menu serves"});
    archive.Field("firstFocus", firstFocus, {.tooltip = "The control focused as it opens"});
    archive.Field("pausesGame", pausesGame);
    archive.Field("clickSound", clickSound);
    archive.Field("style", style);
}

} // namespace Gameplay
