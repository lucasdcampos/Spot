#include "physics_gun.h"
#include <spot/scene/physics/physics_world.h>
#include <spot/scene/rendering/render_hook_system.h>

#include "gameplay/core/body_authority.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/physics_drive.h"
#include "gameplay/core/player_controller.h"

#include <algorithm>
#include <cmath>
#include <utility>

SPOT_COMPONENT(PhysicsGun);

namespace {

namespace Drive = Gameplay::PhysicsDrive;

Spot::Vector3 ToWorld(const Spot::Transform& transform, Spot::Vector3 point) {
    const Spot::Vector4 world = transform.GetWorldMatrix() * Spot::Vector4{point.x, point.y, point.z, 1.0f};
    return {world.x, world.y, world.z};
}

Spot::Vector3 ToLocal(const Spot::Transform& transform, Spot::Vector3 point) {
    const Spot::Vector4 local = Spot::Inverse(transform.GetWorldMatrix()) * Spot::Vector4{point.x, point.y, point.z, 1};
    return {local.x, local.y, local.z};
}

} // namespace

PhysicsGun::PhysicsGun() : HeldItem({"Fire", "Aim", "Reload", "Unfreeze"}) {
    displayName = "Physics Gun";
}

Spot::Entity* PhysicsGun::GetHolder() const {
    Gameplay::Pawn* pawn = GetPawn();
    return pawn ? &pawn->GetEntity() : nullptr;
}

void PhysicsGun::Freeze() {
    // Frozen here, where it stays.
    if (Spot::Entity* prop = GetScene().GetEntity(m_grabbed)) {
        Gameplay::BodyAuthority::LetGo(*prop);
        Gameplay::BodyAuthority::TakeBack(*prop);
    }
    if (Spot::Rigidbody* body = GetGrabbedBody()) {
        body->SetLinearVelocity({});
        body->SetAngularVelocity({});
        body->SetKinematic(true);
        body->GetEntity().AddTag(FrozenTag);
    }
    Release();
}

void PhysicsGun::Release() {
    if (Spot::Entity* prop = GetScene().GetEntity(m_grabbed)) {
        Gameplay::BodyAuthority::KeepOnServer(*prop, false);
        Gameplay::BodyAuthority::LetGo(*prop);
    }
    if (IsGrabbing()) {
        if (Gameplay::Pawn* pawn = GetPawn()) {
            pawn->SetState("HandsBusy", false);
        }
    }
    m_grabbed = Spot::EntityId::Invalid;
    m_grab.Deactivate();
    m_turn.Deactivate();
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

void PhysicsGun::Grab() {
    Gameplay::Pawn* pawn = GetPawn();
    if (!pawn) {
        return;
    }
    const Gameplay::ViewPoint view = pawn->GetViewPoint();
    const auto hit = GetScene().GetOrAddSystem<Spot::PhysicsWorld>().Raycast(view.position, view.direction, range,
                                                                             {.ignore = pawn->GetEntity().GetId()});
    Spot::Entity* prop = hit ? GetScene().GetEntity(hit->entity) : nullptr;
    // What a player's machine simulates comes back here to be held, and stays while it is.
    if (prop) {
        Gameplay::BodyAuthority::TakeBack(*prop);
    }
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
    // A player on another machine holds it there, following the view at once; the server holds its own player's.
    const Spot::NetworkObject* holder = Spot::Network::GetObject(pawn->GetEntity());
    if (!holder || !holder->IsDecidedByClient() || !Gameplay::BodyAuthority::Hold(*prop, holder->GetOwner())) {
        Gameplay::BodyAuthority::KeepOnServer(*prop);
    }
    m_grabPoint = ToLocal(transform, hit->point);
    m_distance = std::max(hit->distance, minDistance);
    const Spot::Quaternion facing = Drive::GetFacing(Drive::GetYaw(view.direction));
    m_rotation = Spot::Quaternion{-facing.x, -facing.y, -facing.z, facing.w} * transform.GetWorldRotation();
    pawn->SetState("HandsBusy", true);
}

void PhysicsGun::OnStart() {
    if (auto* pawn = GetPawn()) {
        m_states = pawn->OnStateChanged.Connect([this](std::string_view state, bool on) {
            if (on && state == "Dead") {
                Release();
            }
        });
    }
}

void PhysicsGun::OnUse(const Gameplay::ItemInput& input) {
    const GrabIntent intent = std::exchange(m_pending, {.hold = m_pending.hold});
    if (!input.active || !GetPawn()) {
        Release();
        return;
    }
    if (input.IsPressed("Unfreeze")) {
        UnfreezeAll();
    }
    // Held through the item's Fire until the grab takes it, then through the grab's.
    if (IsGrabbing() && (!GetGrabbedBody() || !(input.IsDown("Fire") || intent.hold))) {
        Release();
    }
    if (!IsGrabbing() && input.IsPressed("Fire") && input.canUse) {
        Grab();
    }
    if (!IsGrabbing()) {
        return;
    }
    if (intent.freeze) {
        Freeze();
        return;
    }
    // Switch Weapon goes to the next item as the wheel turns toward the player, which pulls the prop closer.
    m_distance = std::clamp(m_distance - intent.pull, minDistance, range);
    if (intent.turning) {
        m_rotation = Drive::TurnByLook(m_rotation, intent.turn, 1.0f);
    }
}

void PhysicsGun::OnPlayerInput(Gameplay::PlayerController& player) {
    m_frame = {};
    if (!IsGrabbing() || !IsEquipped()) {
        m_grab.Deactivate();
        m_turn.Deactivate();
        return;
    }
    m_grab.Activate(&player);
    m_frame.hold = m_grab.IsDown("Fire");
    m_frame.freeze = m_grab.IsPressed("Aim");
    m_frame.pull = m_grab.GetValue("Switch Weapon") * scrollStep;
    m_frame.turning = m_grab.IsDown("Use");
    if (m_frame.turning) {
        m_turn.Activate(&player);
        m_frame.turn = m_turn.GetVector2("Look") * m_lookSensitivity;
    } else {
        m_turn.Deactivate();
    }
    if (Spot::Network::IsServer(GetScene())) {
        Gather(m_frame);
    } else if (Spot::Entity* prop = GetScene().GetEntity(m_grabbed); prop && Spot::Network::HasAuthority(*prop)) {
        // Held here, it moves as this player turns and pulls it, as the server will hear.
        m_distance = std::clamp(m_distance - m_frame.pull, minDistance, range);
        if (m_frame.turning) {
            m_rotation = Drive::TurnByLook(m_rotation, m_frame.turn, 1.0f);
        }
    }
}

void PhysicsGun::Gather(const GrabIntent& intent) {
    m_pending.hold = intent.hold;
    m_pending.freeze = m_pending.freeze || intent.freeze;
    m_pending.pull += intent.pull;
    m_pending.turning = m_pending.turning || intent.turning;
    m_pending.turn += intent.turn;
}

void PhysicsGun::SerializeIntent(Spot::Archive& archive) {
    GrabIntent intent = m_frame;
    archive.Field("hold", intent.hold);
    archive.Field("freeze", intent.freeze);
    archive.Field("pull", intent.pull);
    archive.Field("turning", intent.turning);
    archive.Field("turn", intent.turn);
    if (archive.IsLoading() && std::isfinite(intent.pull) && std::isfinite(intent.turn.x) &&
        std::isfinite(intent.turn.y)) {
        Gather(intent);
    }
}

void PhysicsGun::Replicate(Spot::Archive& archive) {
    Spot::EntityId grabbed = m_grabbed;
    archive.Field("grabbed", grabbed);
    archive.Field("grabPoint", m_grabPoint);
    // Where it holds it, for the machine it is given to, which then turns and pulls it itself.
    float distance = m_distance;
    Spot::Quaternion rotation = m_rotation;
    archive.Field("distance", distance);
    archive.Field("rotation", rotation);
    const Spot::Entity* prop = GetScene().GetEntity(grabbed);
    if (archive.IsLoading() && (grabbed != m_grabbed || !prop || !Spot::Network::HasAuthority(*prop))) {
        m_distance = distance;
        m_rotation = rotation;
    }
    if (archive.IsLoading() && grabbed != m_grabbed) {
        m_grabbed = grabbed;
        if (!IsGrabbing()) {
            if (Spot::AudioSource* source = GetHum()) {
                source->Stop();
            }
        }
    }
}

void PhysicsGun::OnFixedUpdate(float fixedDeltaTime) {
    Spot::Rigidbody* body = GetGrabbedBody();
    const Gameplay::Pawn* pawn = GetPawn();
    // Where the body is simulated: the server, or the holding player's machine it was given to.
    if (!body || !pawn || !Spot::Network::HasAuthority(body->GetEntity())) {
        return;
    }
    const Gameplay::ViewPoint view = pawn->GetViewPoint();
    const Spot::Transform& transform = body->GetEntity().GetTransform();
    const Spot::Vector3 target = view.position + view.direction * m_distance;
    const Spot::Quaternion rotation = Drive::GetFacing(Drive::GetYaw(view.direction)) * m_rotation;
    Drive::Toward(*body, ToWorld(transform, m_grabPoint), target, rotation, fixedDeltaTime, maxSpeed);
}

void PhysicsGun::OnEnable() {
    m_outlineHook = GetScene().GetOrAddSystem<Spot::RenderHookSystem>().AddRenderHook(
        Spot::RenderStage::AfterOpaque,
        [this](const Spot::RenderStageContext& stage) { m_outline.Draw(stage, GetScene().GetEntity(m_grabbed)); });
}

void PhysicsGun::OnDisable() {
    Release();
    GetScene().GetOrAddSystem<Spot::RenderHookSystem>().RemoveRenderHook(m_outlineHook);
    m_outlineHook = Spot::RenderHookId::Invalid;
}

void PhysicsGun::OnLateUpdate(float /*deltaTime*/) {
    // Kinematic too where another machine moves it.
    Spot::Entity* prop = GetScene().GetEntity(m_grabbed);
    const Spot::Rigidbody* body = prop ? prop->GetComponent<Spot::Rigidbody>() : nullptr;
    const Gameplay::Pawn* pawn = GetPawn();
    if (!body || !pawn) {
        return;
    }
    if (Spot::AudioSource* source = GetHum()) {
        if (!source->IsPlaying()) {
            source->Play();
        }
        source->pitch = 1.0f + std::min(Spot::Length(body->GetLinearVelocity()) / 20.0f, 1.0f);
    }
}

Gameplay::ItemStatus PhysicsGun::DescribeStatus() const {
    Gameplay::ItemStatus status = HeldItem::DescribeStatus();
    const bool grabbing = IsGrabbing();
    status.count = grabbing ? "Holding" : "Ready";
    status.countIsWord = true;
    status.summary = grabbing ? "Holding" : "Grab and move props";
    status.urgent = grabbing;
    status.hints =
        grabbing ? std::vector<Gameplay::InputHint>{{"Switch Weapon", "Distance"}, {"Use", "Rotate"}, {"Aim", "Freeze"}}
                 : std::vector<Gameplay::InputHint>{{"Fire", "Hold to grab"}, {"Unfreeze", "Unfreeze all"}};
    return status;
}

Gameplay::ItemPose PhysicsGun::DescribePose() const {
    return {.active = IsGrabbing()};
}

void PhysicsGun::Serialize(Spot::Archive& archive) {
    HeldItem::Serialize(archive);
    archive.Field("range", range, {.min = 0.0f, .max = 1000.0f, .speed = 0.5f});
    archive.Field("minDistance", minDistance, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("scrollStep", scrollStep, {.min = 0.0f, .max = 10.0f, .speed = 0.05f});
    archive.Field("hum", hum, {.tooltip = "The entity whose AudioSource hums while holding a prop"});
    archive.Field("maxSpeed", maxSpeed, {.min = 0.0f, .max = 1000.0f, .speed = 0.5f});
}

Spot::Rigidbody* PhysicsGun::GetGrabbedBody() const {
    Spot::Entity* prop = GetScene().GetEntity(m_grabbed);
    auto* body = prop ? prop->GetComponent<Spot::Rigidbody>() : nullptr;
    // Kinematic too where another machine simulates it.
    return body && body->IsActiveAndEnabled() && (!body->IsKinematic() || !Spot::Network::HasAuthority(*prop))
               ? body
               : nullptr;
}
