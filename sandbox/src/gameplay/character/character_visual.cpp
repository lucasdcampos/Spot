#include "gameplay/character/character_visual.h"

#include "gameplay/character/character_movement.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/physics_drive.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(CharacterVisual);

void CharacterPoseLayer::Serialize(Spot::Archive& archive) {
    archive.Field("state", state);
    archive.Field("layer", layer);
}

void CharacterVisual::Synchronize() {
    const Pawn* pawn = Pawn::Of(GetEntity());
    auto* model = GetScene().GetEntity(modelRoot);
    auto* target = animator.Get(GetScene());
    if (!pawn || !model || !target) {
        return;
    }
    const bool seated = pawn->HasState(seatedState);
    const auto* movement = GetEntity().GetComponent<CharacterMovement>();
    const auto* body = GetEntity().GetComponent<Spot::CharacterController>();
    const bool crouching = !seated && movement && movement->IsCrouching();
    const auto velocity = body && !seated && !pawn->HasState("Dead") && !(movement && movement->IsFlying())
                              ? body->GetVelocity()
                              : Spot::Vector3{};
    const float speed = Spot::Length(Spot::Vector3{velocity.x, 0, velocity.z});
    const float walk = movement ? movement->walkSpeed : 5.0f;
    const float run = movement ? movement->runSpeed : 9.0f;
    target->SetFloat(speedParameter, speed <= walk ? speed / std::max(walk, 0.01f)
                                                   : 1.0f + (speed - walk) / std::max(run - walk, 0.01f));
    if (!seatedLayer.empty()) {
        target->SetLayerWeight(seatedLayer, seated ? 1.0f : 0.0f);
    }
    if (!crouchLayer.empty()) {
        target->SetLayerWeight(crouchLayer, crouching ? 1.0f : 0.0f);
    }
    for (const auto& pose : stateLayers) {
        target->SetLayerWeight(pose.layer, pawn->HasState(pose.state) ? 1.0f : 0.0f);
    }
    model->GetTransform().SetPosition(seated ? seatedOffset : standingOffset);
    if (seated) {
        model->GetTransform().SetRotation({});
    } else {
        model->GetTransform().SetWorldRotation(
            PhysicsDrive::GetFacing(PhysicsDrive::GetYaw(pawn->GetViewPoint().direction)));
    }
}

void CharacterVisual::OnUpdate(float /*deltaTime*/) {
    Synchronize();
}

void CharacterVisual::Serialize(Spot::Archive& archive) {
    archive.Field("modelRoot", modelRoot);
    archive.Field("animator", animator);
    archive.Field("speedParameter", speedParameter);
    archive.Field("seatedLayer", seatedLayer);
    archive.Field("crouchLayer", crouchLayer);
    archive.Field("seatedState", seatedState);
    archive.Field("stateLayers", stateLayers);
    archive.Field("standingOffset", standingOffset);
    archive.Field("seatedOffset", seatedOffset);
}

} // namespace Gameplay
