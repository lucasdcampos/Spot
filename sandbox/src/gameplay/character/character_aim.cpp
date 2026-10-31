#include "gameplay/character/character_aim.h"

#include "gameplay/core/pawn.h"

namespace Gameplay {

SPOT_COMPONENT(CharacterAim);

void CharacterAim::OnUpdate(float /*deltaTime*/) {
    const Pawn* pawn = Pawn::Of(GetEntity());
    if (!pawn) {
        return;
    }
    const ViewPoint view = pawn->GetViewPoint();
    const Spot::Vector3 point = view.position + Spot::Normalize(view.direction) * distance;
    if (auto* constraint = aim.Get(GetScene())) {
        constraint->SetTargetPosition(point);
    }
    if (auto* constraint = look.Get(GetScene())) {
        constraint->SetTargetPosition(point);
        constraint->SetWeight(pawn->HasAnyState(hiddenWhen) ? 0.0f : 1.0f);
    }
}

void CharacterAim::Serialize(Spot::Archive& archive) {
    archive.Field("look", look, {.tooltip = "The aim constraint that turns the head"});
    archive.Field("aim", aim, {.tooltip = "The aim constraint that turns the body to aim what is held"});
    archive.Field("distance", distance, {.min = 0.5f, .max = 1000.0f});
    archive.Field("hiddenWhen", hiddenWhen);
}

} // namespace Gameplay
