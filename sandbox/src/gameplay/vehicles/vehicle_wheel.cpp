#include "gameplay/vehicles/vehicle_wheel.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(VehicleWheel);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};
constexpr Spot::Vector3 Right = {1.0f, 0.0f, 0.0f};

} // namespace

void VehicleWheel::OnLateUpdate(float /*deltaTime*/) {
    Spot::Entity* entity = GetScene().GetEntity(visual);
    if (!entity) {
        return;
    }
    Spot::Transform& transform = entity->GetTransform();
    transform.SetPosition(Up * std::clamp(m_compression, -droopTravel, bumpTravel));
    // Rolling forward turns the top of the wheel forward; left wheels are the right one turned around.
    const Spot::Quaternion side = Spot::Quaternion::FromAxisAngle(Up, m_left ? Spot::Pi : 0.0f);
    transform.SetRotation(Spot::Quaternion::FromAxisAngle(Up, m_steer) *
                          Spot::Quaternion::FromAxisAngle(Right, -m_angle) * side);
}

void VehicleWheel::Serialize(Spot::Archive& archive) {
    archive.Field("radius", radius, {.min = 0.05f, .max = 2.0f, .speed = 0.005f});
    archive.Field("width", width, {.min = 0.02f, .max = 1.0f, .speed = 0.005f});
    archive.Field("inertia", inertia, {.min = 0.01f, .max = 100.0f, .speed = 0.01f, .tooltip = "kg m²"});
    archive.Field("springRate", springRate, {.min = 0.0f, .max = 1000000.0f, .speed = 100.0f, .tooltip = "N/m"});
    archive.Field("damping", damping, {.min = 0.0f, .max = 100000.0f, .speed = 10.0f, .tooltip = "N s/m"});
    archive.Field("bumpTravel", bumpTravel, {.min = 0.0f, .max = 1.0f, .speed = 0.005f});
    archive.Field("droopTravel", droopTravel, {.min = 0.0f, .max = 1.0f, .speed = 0.005f});
    archive.Field("maxSteer", maxSteer,
                  {.min = -1.2f, .max = 1.2f, .speed = 0.005f, .angle = true, .tooltip = "0 for a fixed wheel"});
    archive.Field("driveShare", driveShare, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
    archive.Field("brakeShare", brakeShare, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
    archive.Field("handbrake", handbrake);
    archive.Field("axle", axle, {.min = 0.0f, .max = 16.0f, .tooltip = "Wheels of an axle share an anti-roll bar"});
    archive.Field("visual", visual, {.tooltip = "Moves with the suspension, steers and spins"});
}

} // namespace Gameplay
