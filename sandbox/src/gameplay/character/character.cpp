#include "gameplay/character/character.h"

#include "gameplay/camera/first_person_view.h"
#include "gameplay/character/character_movement.h"
#include "gameplay/core/physics_drive.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/health/fall_damage.h"
#include "gameplay/health/health.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

SPOT_COMPONENT(Character);

Spot::EntityId Character::GetViewCamera() const {
    return m_thirdPerson ? thirdPersonCamera : Pawn::GetViewCamera();
}

bool Character::SetThirdPerson(bool enabled) {
    const auto* entity = GetScene().GetEntity(thirdPersonCamera);
    if (enabled && (!entity || !entity->GetComponent<Spot::Camera>())) {
        return false;
    }
    const auto previous = GetViewCamera();
    m_thirdPerson = enabled;
    SetState("ThirdPerson", enabled);
    if (dynamic_cast<PlayerController*>(GetController())) {
        if (auto* old = GetScene().GetEntity(previous)) {
            if (auto* camera = old->GetComponent<Spot::Camera>()) {
                camera->SetEnabled(false);
            }
        }
        if (auto* current = GetScene().GetEntity(GetViewCamera())) {
            if (auto* camera = current->GetComponent<Spot::Camera>()) {
                camera->SetEnabled(true);
            }
        }
    }
    return true;
}

ViewPoint Character::GetViewPoint() const {
    // Where the view aims, which a camera shake leaves steady.
    if (const auto* view = GetEntity().GetComponent<FirstPersonView>()) {
        return {.position = view->GetEyePosition(),
                .direction = view->GetAimDirection(),
                .rotation = view->GetViewRotation()};
    }
    return Pawn::GetViewPoint();
}

PawnMotion Character::GetMotion() const {
    const auto* movement = GetEntity().GetComponent<CharacterMovement>();
    if (!movement || movement->IsFlying()) {
        return {.stepPhase = movement ? movement->GetStepPhase() : 0.0f};
    }
    return {.stepPhase = movement->GetStepPhase(), .bobWeight = movement->GetBobWeight()};
}

void Character::Place(Spot::Vector3 position, Spot::Vector3 forward, Spot::Vector3 velocity) {
    GetEntity().GetTransform().SetWorldPosition(position);
    GetEntity().GetTransform().SetWorldRotation({});
    if (auto* view = GetEntity().GetComponent<FirstPersonView>()) {
        view->SetLook(PhysicsDrive::GetYaw(forward), 0.0f);
    }
    if (auto* movement = GetEntity().GetComponent<CharacterMovement>()) {
        movement->Push(velocity);
    }
}

void Character::ResetMotion() {
    Pawn::ResetMotion();
    if (auto* movement = GetEntity().GetComponent<CharacterMovement>()) {
        movement->SetVelocity({});
    }
}

void Character::OnStart() {
    SetState("OnFoot", true);
    // Hurts jolt the view and the gamepad.
    if (auto* health = GetEntity().GetComponent<Health>()) {
        m_damaged = health->OnDamaged.Connect([this](const DamageInfo&, float taken) {
            Spot::Input::RumbleGamepad(0.6f, 0.9f, 0.2f);
            if (auto* view = GetEntity().GetComponent<FirstPersonView>()) {
                view->Kick(std::min(taken * 0.002f, 0.08f), 0.3f);
            }
        });
    }
    m_landed = OnLanded.Connect([this](float speed) {
        if (auto* fall = GetEntity().GetComponent<FallDamage>()) {
            fall->Land(speed);
        }
    });
}

void Character::Serialize(Spot::Archive& archive) {
    Pawn::Serialize(archive);
    archive.Field("thirdPersonCamera", thirdPersonCamera, {.tooltip = "Optional camera for inspecting the character"});
}

} // namespace Gameplay
