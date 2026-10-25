#include "car_camera.h"
#include <spot/scene/physics/physics_world.h>

#include "car.h"

#include <algorithm>
#include <cmath>

SPOT_COMPONENT(CarCamera);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

// The same angle between -pi and pi.
float Wrap(float angle) {
    return std::remainder(angle, 2.0f * Spot::Pi);
}

} // namespace

Car* CarCamera::GetCar() const {
    return GetEntity().GetComponentInParent<Car>();
}

float CarCamera::GetCarYaw() const {
    const Car* car = GetCar();
    if (!car) {
        return m_yaw;
    }
    const Spot::Vector3 forward = car->GetEntity().GetTransform().GetWorldRotation() * Spot::Vector3{0.0f, 0.0f, -1.0f};
    return std::atan2(-forward.x, -forward.z);
}

void CarCamera::OnStart() {
    if (const auto* camera = GetEntity().GetComponent<Spot::Camera>()) {
        m_fieldOfView = camera->GetFieldOfView();
    }
}

void CarCamera::OnEnable() {
    m_yaw = GetCarYaw();
    m_orbitYaw = 0.0f;
    m_orbitPitch = 0.0f;
    m_sinceLook = recenterDelay;
}

void CarCamera::OnLateUpdate(float deltaTime) {
    const Car* car = GetCar();
    if (!car) {
        return;
    }
    namespace Input = Spot::Input;
    m_sinceLook += deltaTime;
    if (Input::IsCursorLocked()) {
        // As the player's head turns: up is positive and looks up, which lowers the camera.
        const Spot::Vector2 look = Input::GetActionVector2("Look") * lookSensitivity;
        if (Spot::LengthSquared(look) > 0.0f) {
            m_orbitYaw = Wrap(m_orbitYaw - look.x);
            m_orbitPitch = std::clamp(m_orbitPitch - look.y, -0.5f - pitch, 1.2f - pitch);
            m_sinceLook = 0.0f;
        }
    }
    if (m_sinceLook > recenterDelay && std::abs(car->GetSpeed()) > 2.0f) {
        const float ease = 1.0f - std::exp(-2.0f * deltaTime);
        m_orbitYaw -= m_orbitYaw * ease;
        m_orbitPitch -= m_orbitPitch * ease;
    }
    m_yaw = Wrap(m_yaw + Wrap(GetCarYaw() + m_orbitYaw - m_yaw) * (1.0f - std::exp(-followRate * deltaTime)));

    const Spot::Vector3 focus = car->GetEntity().GetTransform().GetWorldPosition() + Up * lookHeight;
    const float elevation = pitch + m_orbitPitch;
    // Behind a car heading the way of the yaw is toward +Z.
    const Spot::Vector3 back =
        Spot::Quaternion::FromAxisAngle(Up, m_yaw) * Spot::Vector3{0.0f, std::sin(elevation), std::cos(elevation)};
    float reach = distance;
    if (const auto hit = GetScene().GetOrAddSystem<Spot::PhysicsWorld>().SphereCast(
            focus, 0.25f, back, distance, {.ignore = car->GetEntity().GetId()})) {
        reach = std::max(hit->distance, 0.5f);
    }
    Spot::Transform& transform = GetEntity().GetTransform();
    transform.SetWorldPosition(focus + back * reach);
    transform.SetWorldRotation(Spot::Quaternion::LookRotation(-back));
    if (auto* camera = GetEntity().GetComponent<Spot::Camera>(); camera && m_fieldOfView > 0.0f) {
        camera->SetFieldOfView(m_fieldOfView + speedFieldOfView * std::min(std::abs(car->GetSpeed()) / 30.0f, 1.3f));
    }
}

void CarCamera::Serialize(Spot::Archive& archive) {
    archive.Field("distance", distance, {.min = 0.5f, .max = 50.0f, .speed = 0.05f});
    archive.Field("lookHeight", lookHeight, {.min = 0.0f, .max = 10.0f, .speed = 0.01f});
    archive.Field("pitch", pitch, {.min = -0.5f, .max = 1.2f, .speed = 0.005f, .angle = true});
    archive.Field("followRate", followRate, {.min = 0.0f, .max = 30.0f, .speed = 0.05f});
    archive.Field("lookSensitivity", lookSensitivity, {.min = 0.0f, .max = 0.1f, .speed = 0.0001f});
    archive.Field("recenterDelay", recenterDelay, {.min = 0.0f, .max = 10.0f, .speed = 0.05f});
    archive.Field("speedFieldOfView", speedFieldOfView, {.min = 0.0f, .max = 1.0f, .speed = 0.005f, .angle = true});
}
