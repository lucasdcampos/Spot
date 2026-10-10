#include "prop_carrier.h"

#include "physics_gun.h"
#include "player_controller.h"
#include "prop_drive.h"

SPOT_COMPONENT(PropCarrier);

void PropCarrier::PickUp(Spot::Entity& prop) {
    const auto* player = GetEntity().GetComponent<PlayerController>();
    m_held = prop.GetId();
    const Spot::Quaternion facing = PropDrive::GetFacing(player ? player->GetYaw() : 0.0f);
    m_rotation = Spot::Quaternion{-facing.x, -facing.y, -facing.z, facing.w} * prop.GetTransform().GetWorldRotation();
}

void PropCarrier::Drop() {
    m_held = Spot::EntityId::Invalid;
    m_turning = false;
}

void PropCarrier::Throw() {
    if (Spot::Rigidbody* body = GetHeldBody()) {
        const auto* player = GetEntity().GetComponent<PlayerController>();
        body->SetLinearVelocity(player ? player->GetAimDirection() * throwSpeed : Spot::Vector3{});
    }
    Drop();
}

void PropCarrier::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;
    m_threw = false;
    const auto* player = GetEntity().GetComponent<PlayerController>();
    const auto* gun = GetEntity().GetComponent<PhysicsGun>();
    if (!player || (gun && gun->IsGrabbing())) {
        return;
    }
    if (IsHolding() && !GetHeldBody()) {
        Drop();
    }

    if (Input::IsActionPressed("Use")) {
        if (IsHolding()) {
            Drop();
        } else if (const auto hit = GetScene().GetPhysics().Raycast(player->GetEyePosition(), player->GetAimDirection(),
                                                                    reach, {.ignore = GetEntity().GetId()})) {
            Spot::Entity* prop = GetScene().GetEntity(hit->entity);
            const auto* body = prop ? prop->GetComponent<Spot::Rigidbody>() : nullptr;
            if (body && !body->IsKinematic() && body->GetMass() <= maxMass) {
                PickUp(*prop);
            }
        }
    }
    if (!IsHolding()) {
        return;
    }
    if (Input::IsActionPressed("Fire")) {
        Throw();
        m_threw = true;
        return;
    }
    m_turning = Input::IsActionDown("Aim");
    if (m_turning) {
        m_rotation = PropDrive::TurnByLook(m_rotation, Input::GetActionVector2("Look"), player->lookSensitivity);
    }
}

void PropCarrier::OnFixedUpdate(float fixedDeltaTime) {
    Spot::Rigidbody* body = GetHeldBody();
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (!body || !player) {
        return;
    }
    const Spot::Vector3 position = body->GetEntity().GetTransform().GetWorldPosition();
    const Spot::Vector3 target = player->GetEyePosition() + player->GetAimDirection() * holdDistance;
    // Something in the way pulled it too far.
    if (Spot::Distance(position, target) > reach * 2.0f) {
        Drop();
        return;
    }
    const Spot::Quaternion rotation = PropDrive::GetFacing(player->GetYaw()) * m_rotation;
    PropDrive::Toward(*body, position, target, rotation, fixedDeltaTime, 15.0f);
}

void PropCarrier::Serialize(Spot::Archive& archive) {
    archive.Field("reach", reach, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("maxMass", maxMass, {.min = 0.0f, .max = 10000.0f, .speed = 0.5f, .tooltip = "In kilograms"});
    archive.Field("holdDistance", holdDistance, {.min = 0.0f, .max = 10.0f, .speed = 0.05f});
    archive.Field("throwSpeed", throwSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.1f});
}

Spot::Rigidbody* PropCarrier::GetHeldBody() const {
    Spot::Entity* prop = GetScene().GetEntity(m_held);
    auto* body = prop ? prop->GetComponent<Spot::Rigidbody>() : nullptr;
    return body && body->IsActiveAndEnabled() && !body->IsKinematic() ? body : nullptr;
}
