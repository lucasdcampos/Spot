#include "gameplay/vehicles/helicopter.h"

#include "gameplay/vehicles/vehicle_seat.h"

#include <spot/scene/physics/physics_world.h>

#include <algorithm>
#include <cmath>

namespace Gameplay {

SPOT_COMPONENT(Helicopter);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};
constexpr Spot::Vector3 Forward = {0.0f, 0.0f, -1.0f};

} // namespace

Helicopter::Helicopter() {
    controls.accelerate = "FlightPitch";
    controls.brake.clear();
    controls.steer = "FlightYaw";
    controls.handbrake.clear();
    controls.ascend = "FlightAscend";
    controls.descend = "FlightDescend";
    controls.strafe = "FlightRoll";
}

float Helicopter::GetForwardSpeed() const {
    const auto* body = GetEntity().GetComponent<Spot::Rigidbody>();
    return body ? Spot::Dot(body->GetLinearVelocity(), GetEntity().GetTransform().GetWorldRotation() * Forward) : 0.0f;
}

float Helicopter::GetThrottle() const {
    return std::clamp(0.5f + GetInput().lift * 0.5f, 0.0f, 1.0f) * m_rotorSpeed;
}

std::vector<Gauge> Helicopter::DescribeTelemetry() const {
    auto gauges = Vehicle::DescribeTelemetry();
    const auto* body = GetEntity().GetComponent<Spot::Rigidbody>();
    gauges.push_back({"Climb", std::to_string(std::lround(body ? body->GetLinearVelocity().y : 0.0f)) + " m/s", ""});
    gauges.push_back({"Rotor", std::to_string(std::lround(m_rotorSpeed * 100.0f)) + "% rotor", ""});
    return gauges;
}

std::vector<InputHint> Helicopter::DescribeHints() const {
    std::vector<InputHint> hints = {{controls.accelerate, "Pitch"},
                                    {controls.strafe, "Roll"},
                                    {controls.steer, "Turn"},
                                    {controls.ascend, "Ascend"},
                                    {controls.descend, "Descend"}};
    if (const VehicleSeat* seat = GetSeat()) {
        hints.push_back({seat->exitAction, "Exit"});
    }
    return hints;
}

void Helicopter::OnFixedUpdate(float deltaTime) {
    auto* body = GetEntity().GetComponent<Spot::Rigidbody>();
    if (!body || body->IsKinematic() || deltaTime <= 0.0f) {
        return;
    }
    const bool powered = HasDriver() && !HasState("Dead");
    const float target = powered ? 1.0f : 0.0f;
    const float step = deltaTime / std::max(spoolTime, 0.1f);
    m_rotorSpeed += std::clamp(target - m_rotorSpeed, -step, step);
    if (m_rotorSpeed <= 0.0f) {
        return;
    }
    body->WakeUp();
    const VehicleInput input = powered ? GetInput() : VehicleInput{};
    const auto rotation = GetEntity().GetTransform().GetWorldRotation();
    const Spot::Vector3 up = rotation * Up;
    const Spot::Vector3 forward = rotation * Forward;
    const Spot::Vector3 heading = Spot::Normalize(Spot::Vector3{forward.x, 0.0f, forward.z});
    const Spot::Vector3 right = Spot::Cross(heading, Up);
    Spot::Vector2 tilt = {std::clamp(input.strafe, -1.0f, 1.0f), std::clamp(input.drive, -1.0f, 1.0f)};
    if (Spot::LengthSquared(tilt) > 1.0f) {
        tilt = Spot::Normalize(tilt);
    }
    const Spot::Vector3 desiredUp = Spot::Normalize(Up + (heading * tilt.y + right * tilt.x) * std::tan(maxTilt));
    const Spot::Vector3 velocity = body->GetLinearVelocity();
    const Spot::Vector3 angular = body->GetAngularVelocity();
    const float mass = body->GetMass();
    const auto& physics = GetScene().GetOrAddSystem<Spot::PhysicsWorld>();
    const float gravity = std::max(-physics.GetGravity().y * body->GetGravityScale(), 0.0f);
    const float climb = powered ? std::clamp(input.lift, -1.0f, 1.0f) * climbSpeed : -climbSpeed;
    // Tilt loses vertical lift; collective compensates while the rotor has enough authority, never pulling down.
    const float acceleration = std::clamp(
        (gravity + (climb - velocity.y) * liftResponse) / std::max(Spot::Dot(up, Up), 0.3f), 0.0f, gravity * 2.0f);
    const float authority = m_rotorSpeed * m_rotorSpeed;
    body->AddForce(up * (mass * acceleration * authority));
    body->AddForce(Spot::Vector3{-velocity.x, 0.0f, -velocity.z} * (mass * horizontalDrag * authority));
    const Spot::Vector3 leveling = Spot::Cross(up, desiredUp) * attitudeStrength;
    const Spot::Vector3 damping = Spot::Vector3{angular.x, 0.0f, angular.z} * attitudeDamping;
    const float yaw = (-std::clamp(input.steer, -1.0f, 1.0f) * yawRate - angular.y) * attitudeDamping;
    body->AddTorque((leveling - damping + Up * yaw) * (mass * authority));
}

void Helicopter::OnLateUpdate(float deltaTime) {
    const float turn = GetRpm() * (2.0f * Spot::Pi / 60.0f) * deltaTime;
    m_mainAngle = std::fmod(m_mainAngle + turn, 2.0f * Spot::Pi);
    m_tailAngle = std::fmod(m_tailAngle + turn * tailRotorRatio, 2.0f * Spot::Pi);
    if (auto* rotor = GetScene().GetEntity(mainRotor)) {
        rotor->GetTransform().SetRotation(Spot::Quaternion::FromAxisAngle(Up, m_mainAngle));
    }
    if (auto* rotor = GetScene().GetEntity(tailRotor)) {
        rotor->GetTransform().SetRotation(Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, m_tailAngle));
    }
}

void Helicopter::Serialize(Spot::Archive& archive) {
    Vehicle::Serialize(archive);
    archive.Field("rotorRpm", rotorRpm, {.min = 1.0f, .max = 1500.0f});
    archive.Field("spoolTime", spoolTime, {.min = 0.1f, .max = 30.0f});
    archive.Field("maxTilt", maxTilt, {.min = 0.0f, .max = 0.8f, .angle = true});
    archive.Field("climbSpeed", climbSpeed, {.min = 0.0f, .max = 20.0f});
    archive.Field("yawRate", yawRate, {.min = 0.0f, .max = 3.0f, .angle = true});
    archive.Field("liftResponse", liftResponse, {.min = 0.0f, .max = 10.0f});
    archive.Field("attitudeStrength", attitudeStrength, {.min = 0.0f, .max = 100.0f});
    archive.Field("attitudeDamping", attitudeDamping, {.min = 0.0f, .max = 50.0f});
    archive.Field("horizontalDrag", horizontalDrag, {.min = 0.0f, .max = 5.0f});
    archive.Field("tailRotorRatio", tailRotorRatio, {.min = 0.1f, .max = 10.0f});
    archive.Field("mainRotor", mainRotor);
    archive.Field("tailRotor", tailRotor);
}

} // namespace Gameplay
