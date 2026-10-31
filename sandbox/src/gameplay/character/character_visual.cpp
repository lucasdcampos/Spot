#include "gameplay/character/character_visual.h"

#include "gameplay/character/character_movement.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/physics_drive.h"
#include "gameplay/core/players.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(CharacterVisual);

namespace {

void SetModelLayer(Spot::Entity& entity, int layer) {
    entity.SetLayer(layer);
    entity.ForEachChild([layer](Spot::Entity& child) { SetModelLayer(child, layer); });
}

} // namespace

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
    if (localLayer >= 0) {
        const int layer = Players::IsPlayedLocally(*pawn) ? std::clamp(localLayer, 0, 31) : GetEntity().GetLayer();
        if (layer != m_modelLayer) {
            m_modelLayer = layer;
            SetModelLayer(*model, layer);
        }
    }
    const bool seated = pawn->HasState(seatedState);
    const auto* movement = GetEntity().GetComponent<CharacterMovement>();
    const auto* body = GetEntity().GetComponent<Spot::CharacterController>();
    const bool crouching = !seated && movement && movement->IsCrouching();
    // Where another machine decides the pawn, its body does not move it: the speed is how fast it is moved.
    const Spot::Vector3 moved = Spot::Network::HasAuthority(GetEntity()) || !body
                                    ? (body ? body->GetVelocity() : Spot::Vector3{})
                                    : m_shownVelocity;
    const auto velocity =
        body && !seated && !pawn->HasState("Dead") && !(movement && movement->IsFlying()) ? moved : Spot::Vector3{};
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
    if (const auto* ragdoll = model->GetComponent<Spot::Ragdoll>(); ragdoll && ragdoll->IsActive()) {
        return;
    }
    model->GetTransform().SetPosition(seated ? seatedOffset : standingOffset);
    if (seated) {
        model->GetTransform().SetRotation({});
    } else {
        model->GetTransform().SetWorldRotation(
            PhysicsDrive::GetFacing(PhysicsDrive::GetYaw(pawn->GetViewPoint().direction)));
    }
}

void CharacterVisual::OnUpdate(float deltaTime) {
    const Spot::Vector3 position = GetEntity().GetTransform().GetWorldPosition();
    if (m_lastPosition && deltaTime > 0.0f) {
        // Eased, as states arrive a few frames apart.
        const Spot::Vector3 velocity = (position - *m_lastPosition) / deltaTime;
        m_shownVelocity = Spot::Lerp(m_shownVelocity, velocity, std::min(deltaTime * 10.0f, 1.0f));
    }
    m_lastPosition = position;
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
    archive.Field("localLayer", localLayer,
                  {.min = -1, .max = 31, .tooltip = "The model's layer while a local player plays as this pawn"});
}

} // namespace Gameplay
