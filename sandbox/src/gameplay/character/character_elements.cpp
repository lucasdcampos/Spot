#include "gameplay/character/character_elements.h"

#include "gameplay/camera/first_person_view.h"
#include "gameplay/character/character_movement.h"
#include "gameplay/core/pawn.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace Gameplay {

SPOT_COMPONENT(CrosshairElement);

bool CrosshairElement::IsWanted(const HudContext& context) {
    if (!context.pawn) {
        return true;
    }
    const Spot::Entity& entity = context.pawn->GetEntity();
    const auto* view = entity.GetComponent<FirstPersonView>();
    const auto* movement = entity.GetComponent<CharacterMovement>();
    const auto* body = entity.GetComponent<Spot::CharacterController>();
    // It opens up while moving, in the air and as recoil kicks.
    float spread = 0.0f;
    if (body && movement) {
        const Spot::Vector3 velocity = body->GetVelocity();
        spread +=
            std::min(Spot::Length(Spot::Vector3{velocity.x, 0.0f, velocity.z}) / std::max(movement->runSpeed, 0.1f),
                     1.0f) *
            0.5f;
        spread += body->IsGrounded() || movement->IsFlying() ? 0.0f : 0.35f;
    }
    if (view) {
        spread += view->GetRecoil() * 8.0f;
    }
    m_spread += (std::min(spread, 1.0f) - m_spread) * 0.25f;
    return context.pawn->HasAnyState(shownBy) || !context.pawn->HasAnyState(hiddenBy);
}

void CrosshairElement::Refresh(const HudContext& /*context*/) {
    // The lines move out as the aim gets less steady.
    const float gap = 7.0f + std::clamp(m_spread, 0.0f, 1.0f) * 14.0f;
    const std::pair<std::string_view, Spot::Vector2> lines[] = {
        {"Top", {0, gap}}, {"Bottom", {0, -gap}}, {"Left", {-gap, 0}}, {"Right", {gap, 0}}};
    for (const auto& [lineName, position] : lines) {
        if (Spot::Entity* line = GetEntity().FindChild(lineName)) {
            if (auto* rect = line->GetComponent<Spot::UiRect>()) {
                rect->layout.offset = position;
            }
        }
    }
}

void CrosshairElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("hiddenBy", hiddenBy, {.tooltip = "Pawn states it goes with, unless also in one of shownBy"});
    archive.Field("shownBy", shownBy, {.tooltip = "Pawn states it shows with, whatever hiddenBy says"});
}

} // namespace Gameplay
