#include "gameplay/camera/chase_camera.h"
#include <spot/scene/physics/physics_world.h>

#include "gameplay/core/pawn.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

SPOT_COMPONENT(ChaseCamera);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};
constexpr Spot::Vector3 Forward = {0.0f, 0.0f, -1.0f};

// The same angle between -pi and pi.
float Wrap(float angle) {
    return std::remainder(angle, 2.0f * Spot::Pi);
}

} // namespace

Pawn* ChaseCamera::GetPawn() const {
    return Pawn::Of(GetEntity());
}

float ChaseCamera::GetPawnYaw() const {
    const Pawn* pawn = GetPawn();
    if (!pawn) {
        return m_yaw;
    }
    const Spot::Vector3 forward = pawn->GetEntity().GetTransform().GetWorldRotation() * Forward;
    return std::atan2(-forward.x, -forward.z);
}

float ChaseCamera::GetPawnSpeed() const {
    const Pawn* pawn = GetPawn();
    const auto* body = pawn ? pawn->GetEntity().GetComponent<Spot::Rigidbody>() : nullptr;
    if (!body) {
        return 0.0f;
    }
    return Spot::Dot(body->GetLinearVelocity(), pawn->GetEntity().GetTransform().GetWorldRotation() * Forward);
}

void ChaseCamera::Recenter() {
    m_yaw = GetPawnYaw();
    m_orbitYaw = 0.0f;
    m_orbitPitch = 0.0f;
    m_sinceLook = recenterDelay;
}

void ChaseCamera::OnStart() {
    if (const auto* camera = GetEntity().GetComponent<Spot::Camera>()) {
        m_fieldOfView = camera->GetFieldOfView();
    }
}

void ChaseCamera::OnDisable() {
    m_input.Deactivate();
    m_following = false;
}

void ChaseCamera::OnLateUpdate(float deltaTime) {
    const Pawn* pawn = GetPawn();
    auto* camera = GetEntity().GetComponent<Spot::Camera>();
    if (!pawn || !camera || !camera->IsEnabled()) {
        m_input.Deactivate();
        m_following = false;
        return;
    }
    if (!m_following) {
        Recenter();
        m_following = true;
    }
    if (!m_input.Takes(lookAction)) {
        m_input.SetActions({lookAction});
    }
    m_input.Activate(pawn->GetController());
    m_sinceLook += deltaTime;
    if (Spot::Input::IsCursorLocked()) {
        // As a head turns: up is positive and looks up, which lowers the camera.
        const Spot::Vector2 look = m_input.GetVector2(lookAction) * lookSensitivity;
        if (Spot::LengthSquared(look) > 0.0f) {
            m_orbitYaw = Wrap(m_orbitYaw - look.x);
            m_orbitPitch = std::clamp(m_orbitPitch - look.y, -0.5f - pitch, 1.2f - pitch);
            m_sinceLook = 0.0f;
        }
    }
    const float speed = GetPawnSpeed();
    if (m_sinceLook > recenterDelay && std::abs(speed) > 2.0f) {
        const float ease = 1.0f - std::exp(-2.0f * deltaTime);
        m_orbitYaw -= m_orbitYaw * ease;
        m_orbitPitch -= m_orbitPitch * ease;
    }
    m_yaw = Wrap(m_yaw + Wrap(GetPawnYaw() + m_orbitYaw - m_yaw) * (1.0f - std::exp(-followRate * deltaTime)));

    const Spot::Vector3 focus = pawn->GetEntity().GetTransform().GetWorldPosition() + Up * lookHeight;
    const float elevation = pitch + m_orbitPitch;
    // Behind a pawn heading the way of the yaw is toward +Z.
    const Spot::Vector3 back =
        Spot::Quaternion::FromAxisAngle(Up, m_yaw) * Spot::Vector3{0.0f, std::sin(elevation), std::cos(elevation)};
    float reach = distance;
    if (const auto hit = GetScene().GetOrAddSystem<Spot::PhysicsWorld>().SphereCast(
            focus, 0.25f, back, distance, {.ignore = pawn->GetEntity().GetId()})) {
        reach = std::max(hit->distance, 0.5f);
    }
    Spot::Transform& transform = GetEntity().GetTransform();
    transform.SetWorldPosition(focus + back * reach);
    transform.SetWorldRotation(Spot::Quaternion::LookRotation(-back));
    if (m_fieldOfView > 0.0f) {
        camera->SetFieldOfView(m_fieldOfView + speedFieldOfView * std::min(std::abs(speed) / 30.0f, 1.3f));
    }
}

void ChaseCamera::Serialize(Spot::Archive& archive) {
    archive.Field("distance", distance, {.min = 0.5f, .max = 50.0f, .speed = 0.05f});
    archive.Field("lookHeight", lookHeight, {.min = 0.0f, .max = 10.0f, .speed = 0.01f});
    archive.Field("pitch", pitch, {.min = -0.5f, .max = 1.2f, .speed = 0.005f, .angle = true});
    archive.Field("followRate", followRate, {.min = 0.0f, .max = 30.0f, .speed = 0.05f});
    archive.Field("lookSensitivity", lookSensitivity, {.min = 0.0f, .max = 0.1f, .speed = 0.0001f});
    archive.Field("recenterDelay", recenterDelay, {.min = 0.0f, .max = 10.0f, .speed = 0.05f});
    archive.Field("speedFieldOfView", speedFieldOfView, {.min = 0.0f, .max = 1.0f, .speed = 0.005f, .angle = true});
    archive.Field("lookAction", lookAction);
}

} // namespace Gameplay
