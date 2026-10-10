#include "physics_gun.h"

#include "player_controller.h"
#include "prop_drive.h"

#include <algorithm>

SPOT_COMPONENT(PhysicsGun);

namespace {

Spot::Vector3 ToWorld(const Spot::Transform& transform, Spot::Vector3 point) {
    const Spot::Vector4 world = transform.GetWorldMatrix() * Spot::Vector4{point.x, point.y, point.z, 1.0f};
    return {world.x, world.y, world.z};
}

Spot::Vector3 ToLocal(const Spot::Transform& transform, Spot::Vector3 point) {
    const Spot::Quaternion rotation = transform.GetWorldRotation();
    const Spot::Quaternion inverse{-rotation.x, -rotation.y, -rotation.z, rotation.w};
    // Props are not scaled by the gun, so the scale is left out: the point is a direction and a distance.
    return inverse * (point - transform.GetWorldPosition());
}

} // namespace

void PhysicsGun::Freeze() {
    if (Spot::Rigidbody* body = GetGrabbedBody()) {
        body->SetLinearVelocity({});
        body->SetAngularVelocity({});
        body->SetKinematic(true);
        body->GetEntity().AddTag(FrozenTag);
    }
    Release();
}

void PhysicsGun::Release() {
    m_grabbed = Spot::EntityId::Invalid;
    m_turning = false;
}

void PhysicsGun::UnfreezeAll() {
    for (Spot::Entity* entity : GetScene().FindEntitiesWithTag(FrozenTag)) {
        entity->RemoveTag(FrozenTag);
        if (auto* body = entity->GetComponent<Spot::Rigidbody>()) {
            body->SetKinematic(false);
            body->WakeUp();
        }
    }
}

bool PhysicsGun::IsActive() const {
    const auto* player = GetEntity().GetComponent<PlayerController>();
    return player && player->activeTool == PlayerController::Tool::PhysicsGun;
}

void PhysicsGun::Grab() {
    const auto* player = GetEntity().GetComponent<PlayerController>();
    const auto hit = GetScene().GetPhysics().Raycast(player->GetEyePosition(), player->GetAimDirection(), range,
                                                     {.ignore = GetEntity().GetId()});
    Spot::Entity* prop = hit ? GetScene().GetEntity(hit->entity) : nullptr;
    auto* body = prop ? prop->GetComponent<Spot::Rigidbody>() : nullptr;
    if (!body || (body->IsKinematic() && !prop->HasTag(FrozenTag))) {
        return;
    }
    // Grabbing a frozen prop frees it.
    if (prop->HasTag(FrozenTag)) {
        prop->RemoveTag(FrozenTag);
        body->SetKinematic(false);
    }
    const Spot::Transform& transform = prop->GetTransform();
    m_grabbed = prop->GetId();
    m_grabPoint = ToLocal(transform, hit->point);
    m_distance = std::max(hit->distance, minDistance);
    const Spot::Quaternion facing = PropDrive::GetFacing(player->GetYaw());
    m_rotation = Spot::Quaternion{-facing.x, -facing.y, -facing.z, facing.w} * transform.GetWorldRotation();
}

void PhysicsGun::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (!IsActive() || !player) {
        Release();
        return;
    }
    if (Input::IsKeyPressed(Spot::Key::U)) {
        UnfreezeAll();
    }
    if (IsGrabbing() && (!GetGrabbedBody() || !Input::IsMouseButtonDown(Spot::MouseButton::Left))) {
        Release();
    }
    if (!IsGrabbing() && Input::IsMouseButtonPressed(Spot::MouseButton::Left)) {
        Grab();
    }
    if (!IsGrabbing()) {
        return;
    }
    if (Input::IsMouseButtonPressed(Spot::MouseButton::Right)) {
        Freeze();
        return;
    }
    m_distance = std::clamp(m_distance + Input::GetScrollDelta().y * scrollStep, minDistance, range);
    m_turning = Input::IsKeyDown(Spot::Key::E);
    if (m_turning) {
        m_rotation = PropDrive::TurnByMouse(m_rotation, Input::GetMouseDelta(), player->lookSensitivity);
    }
}

void PhysicsGun::OnFixedUpdate(float fixedDeltaTime) {
    Spot::Rigidbody* body = GetGrabbedBody();
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (!body || !player) {
        return;
    }
    const Spot::Transform& transform = body->GetEntity().GetTransform();
    const Spot::Vector3 target = player->GetEyePosition() + player->GetAimDirection() * m_distance;
    const Spot::Quaternion rotation = PropDrive::GetFacing(player->GetYaw()) * m_rotation;
    PropDrive::Toward(*body, ToWorld(transform, m_grabPoint), target, rotation, fixedDeltaTime, maxSpeed);
}

void PhysicsGun::OnLateUpdate(float /*deltaTime*/) {
    const Spot::Rigidbody* body = GetGrabbedBody();
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (!body || !player) {
        return;
    }
    // The beam, until the rendering milestones give it a look of its own.
    const Spot::Vector3 muzzle =
        player->GetEyePosition() + player->GetViewRotation() * Spot::Vector3{0.25f, -0.2f, -0.4f};
    Spot::DebugDraw::Line(muzzle, ToWorld(body->GetEntity().GetTransform(), m_grabPoint),
                          {.color = {0.3f, 0.7f, 1.0f, 1.0f}});
}

void PhysicsGun::Serialize(Spot::Archive& archive) {
    archive.Field("range", range, {.min = 0.0f, .max = 1000.0f, .speed = 0.5f});
    archive.Field("minDistance", minDistance, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("scrollStep", scrollStep, {.min = 0.0f, .max = 10.0f, .speed = 0.05f});
    archive.Field("maxSpeed", maxSpeed, {.min = 0.0f, .max = 1000.0f, .speed = 0.5f});
}

Spot::Rigidbody* PhysicsGun::GetGrabbedBody() const {
    Spot::Entity* prop = GetScene().GetEntity(m_grabbed);
    auto* body = prop ? prop->GetComponent<Spot::Rigidbody>() : nullptr;
    return body && body->IsActiveAndEnabled() && !body->IsKinematic() ? body : nullptr;
}
