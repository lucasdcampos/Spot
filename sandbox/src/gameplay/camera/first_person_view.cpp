#include "gameplay/camera/first_person_view.h"

#include "gameplay/camera/camera_shake.h"
#include "gameplay/core/pawn.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

SPOT_COMPONENT(FirstPersonView);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

} // namespace

Spot::Quaternion FirstPersonView::GetViewRotation() const {
    return Spot::Quaternion::FromAxisAngle(Up, m_yaw) *
           Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, std::clamp(m_pitch + m_recoil, -1.5f, 1.5f));
}

void FirstPersonView::SetLook(float yaw, float pitch) {
    m_yaw = yaw;
    m_pitch = std::clamp(pitch, -1.5f, 1.5f);
    TurnHead(GetViewRotation());
}

Spot::Vector3 FirstPersonView::GetEyePosition() const {
    const Spot::Entity* eye = GetHead();
    return (eye ? eye : &GetEntity())->GetTransform().GetWorldPosition();
}

Spot::Vector3 FirstPersonView::GetAimDirection() const {
    return GetViewRotation() * Spot::Vector3{0.0f, 0.0f, -1.0f};
}

float FirstPersonView::GetFieldOfView() const {
    if (m_fieldOfView > 0) {
        return m_fieldOfView;
    }
    const auto* entity = GetHead();
    const auto* camera = entity ? entity->GetComponent<Spot::Camera>() : nullptr;
    return camera ? camera->GetFieldOfView() : Spot::ToRadians(60.0f);
}

void FirstPersonView::SetFieldOfView(float radians) {
    m_fieldOfView = std::clamp(std::isfinite(radians) ? radians : Spot::ToRadians(60.0f), Spot::ToRadians(30.0f),
                               Spot::ToRadians(130.0f));
    if (auto* entity = GetHead()) {
        if (auto* camera = entity->GetComponent<Spot::Camera>()) {
            camera->SetFieldOfView(2.0f * std::atan(std::tan(m_fieldOfView * 0.5f) / m_zoom));
        }
    }
}

void FirstPersonView::ApplySettings(const UserSettings& settings) {
    lookSensitivity = settings.sensitivity;
    SetFieldOfView(Spot::ToRadians(settings.fieldOfView));
}

void FirstPersonView::ReadSettings(UserSettings& settings) const {
    settings.sensitivity = lookSensitivity;
    settings.fieldOfView = Spot::ToDegrees(GetFieldOfView());
}

void FirstPersonView::OnStart() {
    if (const Spot::Entity* entity = GetHead()) {
        if (const auto* camera = entity->GetComponent<Spot::Camera>()) {
            m_fieldOfView = camera->GetFieldOfView();
        }
    }
}

void FirstPersonView::Kick(float radians, float recoveryTime) {
    auto& tweens = Spot::TweenSystem::Get(GetScene());
    tweens.Cancel(m_recoilTween);
    m_recoil = std::clamp(m_recoil + radians, 0.0f, 0.2f);
    m_recoilTween = tweens.Play(*this, Spot::Tween::Value(
                                           std::max(recoveryTime, 0.01f), m_recoil, 0.0f,
                                           [this](float value) { m_recoil = value; }, Spot::Ease::QuadOut));
}

void FirstPersonView::OnDisable() {
    if (auto* tweens = GetScene().GetSystem<Spot::TweenSystem>()) {
        tweens->Cancel(m_recoilTween);
    }
    m_recoil = 0;
    m_input.Deactivate();
}

void FirstPersonView::OnUpdate(float deltaTime) {
    const Pawn* pawn = Pawn::Of(GetEntity());
    const float zoom = pawn ? pawn->GetZoom() : 1.0f;
    m_zoom += (zoom - m_zoom) * std::min(zoomSpeed * deltaTime * 60.0f, 1.0f);
    const float run = pawn && pawn->HasState(runState) ? 1.0f : 0.0f;
    m_run += (run - m_run) * (1.0f - std::exp(-6.0f * deltaTime));

    // A free cursor belongs to the rest of the screen; tools that turn props with the mouse, and the weapon wheel,
    // take Look from this context while they do.
    if (!m_input.Takes(lookAction)) {
        m_input.SetActions({lookAction});
    }
    m_input.Activate(Pawn::GetControllerOf(GetEntity()));
    if (Spot::Input::IsCursorLocked()) {
        // Up is positive, so the pitch rises with it.
        const Spot::Vector2 look = m_input.GetVector2(lookAction) * (lookSensitivity / m_zoom);
        m_yaw -= look.x;
        m_pitch = std::clamp(m_pitch + look.y, -1.5f, 1.5f);
    }
    // Now rather than once the frame is over, so what looks where the pawn looks this frame looks the new way.
    TurnHead(GetViewRotation());
}

void FirstPersonView::OnLateUpdate(float /*deltaTime*/) {
    Spot::Entity* entity = GetHead();
    if (!entity) {
        return;
    }
    Spot::Quaternion rotation = GetViewRotation();
    if (const auto* shake = GetEntity().GetComponent<CameraShake>()) {
        rotation = rotation * shake->GetRotation();
    }
    TurnHead(rotation);
    if (auto* camera = entity->GetComponent<Spot::Camera>(); camera && m_fieldOfView > 0.0f) {
        // What a lens of that magnification sees, a little wider running.
        camera->SetFieldOfView(2.0f * std::atan(std::tan(m_fieldOfView * 0.5f) * (1.0f + 0.07f * m_run) / m_zoom));
    }
}

void FirstPersonView::Serialize(Spot::Archive& archive) {
    archive.Field("lookSensitivity", lookSensitivity,
                  {.min = 0.0f, .max = 0.1f, .speed = 0.0001f, .tooltip = "Radians per pixel the mouse moves"});
    archive.Field("zoomSpeed", zoomSpeed, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
    archive.Field("head", head, {.tooltip = "Turns where the pawn looks, with the camera"});
    archive.Field("lookAction", lookAction);
    archive.Field("runState", runState, {.tooltip = "The pawn state during which the view widens"});
}

Spot::Entity* FirstPersonView::GetHead() const {
    return GetEntity().GetScene().GetEntity(head);
}

void FirstPersonView::TurnHead(Spot::Quaternion rotation) {
    if (Spot::Entity* entity = GetHead()) {
        entity->GetTransform().SetRotation(rotation);
    }
}

} // namespace Gameplay
