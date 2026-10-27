#include "gameplay/hud/hud.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/core/players.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace Gameplay {

SPOT_COMPONENT(Hud);

namespace {

// Seconds to fade the introduction of the controls out.
constexpr float FadeTime = 0.18f;

using Element = std::pair<Spot::Entity*, HudElement*>;

void CollectElements(Spot::Entity& entity, std::vector<Element>& found) {
    entity.ForEachChild([&](Spot::Entity& child) {
        child.ForEachComponent<HudElement>([&](HudElement& element) { found.emplace_back(&child, &element); });
        CollectElements(child, found);
    });
}

bool HasElements(Spot::Entity& entity) {
    bool found = false;
    entity.ForEachChild([&](Spot::Entity& child) {
        if (!found) {
            found = child.HasComponent<HudElement>() || HasElements(child);
        }
    });
    return found;
}

} // namespace

void HudElement::Serialize(Spot::Archive& archive) {
    archive.Field("showWhen", showWhen, {.tooltip = "Pawn states it shows only with, all of them"});
    archive.Field("hideWhen", hideWhen, {.tooltip = "Pawn states it hides with, any of them"});
}

void HudElement::SetText(Spot::EntityId id, std::string_view text) const {
    if (auto* widget = Get<Spot::UiText>(id); widget && widget->text != text) {
        widget->text.assign(text);
    }
}

void HudElement::SetActive(Spot::EntityId id, bool active) const {
    if (Spot::Entity* entity = GetScene().GetEntity(id); entity && entity->IsActiveSelf() != active) {
        entity->SetActive(active);
    }
}

float Hud::GetIntro() const {
    return std::clamp((introTime - m_time) / FadeTime, 0.0f, 1.0f);
}

void Hud::OnStart() {
    if (!HasElements(GetEntity()) && interface) {
        if (Spot::Entity* root = GetScene().Instantiate(interface, {.parent = &GetEntity()})) {
            m_interface = root->GetId();
        }
    }
}

void Hud::OnEnable() {
    if (Spot::Entity* root = GetScene().GetEntity(m_interface)) {
        root->SetActive(true);
    }
}

void Hud::OnDisable() {
    if (Spot::Entity* root = GetScene().GetEntity(m_interface)) {
        root->SetActive(false);
    }
}

void Hud::OnDestroy() {
    if (Spot::Entity* root = GetScene().GetEntity(m_interface)) {
        root->Destroy();
    }
}

HudContext Hud::MakeContext(float deltaTime) {
    HudContext context;
    context.hud = this;
    context.player = Players::Get(GetScene(), static_cast<std::size_t>(std::max(playerIndex, 0)));
    context.pawn = context.player ? context.player->GetPawn() : nullptr;
    if (context.pawn) {
        context.states = context.pawn->GetStates();
        context.occupant = context.pawn->GetOccupant();
    }
    const float unscaled = Spot::Time::GetUnscaledDeltaTime();
    context.deltaTime = unscaled > 0.0f ? unscaled : deltaTime;
    m_time += context.deltaTime;
    context.time = m_time;
    const Spot::InputState* state = Spot::Input::GetState();
    context.inGame = Spot::Input::IsCursorLocked() && !(state && state->IsCaptured());
    return context;
}

void Hud::OnLateUpdate(float deltaTime) {
    const HudContext context = MakeContext(deltaTime);
    const auto holds = [&](const std::string& state) {
        return std::ranges::find(context.states, state) != context.states.end();
    };
    std::vector<Element> found;
    CollectElements(GetEntity(), found);
    for (std::size_t i = 0; i < found.size();) {
        Spot::Entity* entity = found[i].first;
        bool any = false;
        do {
            HudElement* element = found[i++].second;
            const bool wanted = element->IsWanted(context);
            element->m_shown = element->IsEnabled() && std::ranges::all_of(element->showWhen, holds) &&
                               std::ranges::none_of(element->hideWhen, holds) && wanted;
            any = any || element->m_shown;
        } while (i < found.size() && found[i].first == entity);
        if (entity->IsActiveSelf() != any) {
            entity->SetActive(any);
        }
    }
    for (const auto& [entity, element] : found) {
        if (element->m_shown && entity->IsActiveInHierarchy()) {
            element->Refresh(context);
        }
    }
}

void Hud::Serialize(Spot::Archive& archive) {
    archive.Field("interface", interface, {.tooltip = "Made below it when it has no elements"});
    archive.Field("font", font);
    archive.Field("introTime", introTime,
                  {.min = 0, .max = 60, .speed = .05f, .tooltip = "Seconds the controls show as the game starts"});
    archive.Field("playerIndex", playerIndex, {.min = 0, .max = 16, .tooltip = "Whose HUD it is"});
}

} // namespace Gameplay
