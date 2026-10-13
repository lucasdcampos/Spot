#include "physics_gun.h"

#include "particle_effects.h"
#include "player_controller.h"
#include "prop_carrier.h"
#include "prop_drive.h"
#include "weapon_holder.h"

#include <algorithm>

SPOT_COMPONENT(PhysicsGun);

namespace {

Spot::Vector3 ToWorld(const Spot::Transform& transform, Spot::Vector3 point) {
    const Spot::Vector4 world = transform.GetWorldMatrix() * Spot::Vector4{point.x, point.y, point.z, 1.0f};
    return {world.x, world.y, world.z};
}

Spot::Vector3 ToLocal(const Spot::Transform& transform, Spot::Vector3 point) {
    const Spot::Vector4 local = Spot::Inverse(transform.GetWorldMatrix()) * Spot::Vector4{point.x, point.y, point.z, 1};
    return {local.x, local.y, local.z};
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
    m_particleEmission = 0;
    if (Spot::AudioSource* source = GetHum()) {
        source->Stop();
    }
}

Spot::AudioSource* PhysicsGun::GetHum() const {
    Spot::Entity* entity = GetScene().GetEntity(hum);
    return entity ? entity->GetComponent<Spot::AudioSource>() : nullptr;
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
    const auto* holder = GetEntity().GetComponent<WeaponHolder>();
    return holder && holder->IsPhysicsGunSelected();
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
    if (Input::IsActionPressed("Unfreeze")) {
        UnfreezeAll();
    }
    if (IsGrabbing() && (!GetGrabbedBody() || !Input::IsActionDown("Fire"))) {
        Release();
    }
    const auto* carrier = GetEntity().GetComponent<PropCarrier>();
    if (!IsGrabbing() && Input::IsActionPressed("Fire") && !(carrier && carrier->HasClick())) {
        Grab();
    }
    if (!IsGrabbing()) {
        return;
    }
    if (Input::IsActionPressed("Aim")) {
        Freeze();
        return;
    }
    // Switch Weapon goes to the next weapon as the wheel turns toward the player, which pulls the prop closer.
    m_distance = std::clamp(m_distance - Input::GetActionValue("Switch Weapon") * scrollStep, minDistance, range);
    m_turning = Input::IsActionDown("Use");
    if (m_turning) {
        m_rotation = PropDrive::TurnByLook(m_rotation, Input::GetActionVector2("Look"), player->lookSensitivity);
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

void PhysicsGun::OnEnable() {
    m_beamHook =
        GetScene().AddRenderHook(Spot::RenderStage::AfterOpaque,
                                 [this](const Spot::RenderStageContext& stage) { m_beamLines.Draw(stage, m_beam); });
}

void PhysicsGun::OnDisable() {
    Release();
    GetScene().RemoveRenderHook(m_beamHook);
    m_beamHook = Spot::RenderHookId::Invalid;
    m_beam.clear();
    m_particleEmission = 0;
}

void PhysicsGun::OnLateUpdate(float deltaTime) {
    m_beam.clear();
    const Spot::Rigidbody* body = GetGrabbedBody();
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (!body || !player) {
        return;
    }
    if (Spot::AudioSource* source = GetHum()) {
        if (!source->IsPlaying()) {
            source->Play();
        }
        source->pitch = 1.0f + std::min(Spot::Length(body->GetLinearVelocity()) / 20.0f, 1.0f);
    }
    // Brighter than white, so it glows through bloom.
    const Spot::Vector3 muzzle =
        player->GetEyePosition() + player->GetViewRotation() * Spot::Vector3{0.25f, -0.2f, -0.4f};
    ParticleEffects::Beam(GetScene(), muzzle, ToWorld(body->GetEntity().GetTransform(), m_grabPoint), deltaTime,
                          m_particleEmission);
    m_beam.push_back({.from = muzzle,
                      .to = ToWorld(body->GetEntity().GetTransform(), m_grabPoint),
                      .color = {1.2f, 2.8f, 4.0f, 1.0f}});
}

void PhysicsGun::Serialize(Spot::Archive& archive) {
    archive.Field("range", range, {.min = 0.0f, .max = 1000.0f, .speed = 0.5f});
    archive.Field("minDistance", minDistance, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("scrollStep", scrollStep, {.min = 0.0f, .max = 10.0f, .speed = 0.05f});
    archive.Field("hum", hum, {.tooltip = "The entity whose AudioSource hums while holding a prop"});
    archive.Field("maxSpeed", maxSpeed, {.min = 0.0f, .max = 1000.0f, .speed = 0.5f});
}

Spot::Rigidbody* PhysicsGun::GetGrabbedBody() const {
    Spot::Entity* prop = GetScene().GetEntity(m_grabbed);
    auto* body = prop ? prop->GetComponent<Spot::Rigidbody>() : nullptr;
    return body && body->IsActiveAndEnabled() && !body->IsKinematic() ? body : nullptr;
}
