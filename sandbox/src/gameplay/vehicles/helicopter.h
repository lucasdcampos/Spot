#pragma once

#include "gameplay/vehicles/vehicle.h"

namespace Gameplay {

// An assisted helicopter on its Rigidbody: the rotor tilts to fly forward and back, the pedals turn it, and the
// collective commands a climb rate. Releasing the controls levels it and holds height once the rotor is at speed.
// The origin is on the ground beneath the cabin, with -Z forward. Seats, cameras and saving use Vehicle's contracts.
class Helicopter : public Vehicle {
public:
    Helicopter();

    float rotorRpm = 420.0f;
    float spoolTime = 3.0f;
    float maxTilt = Spot::ToRadians(22.0f);
    float climbSpeed = 4.0f;
    float yawRate = Spot::ToRadians(65.0f);
    float liftResponse = 2.0f;
    float attitudeStrength = 18.0f;
    float attitudeDamping = 7.0f;
    float horizontalDrag = 0.35f;
    float tailRotorRatio = 5.0f;
    Spot::EntityId mainRotor = Spot::EntityId::Invalid;
    Spot::EntityId tailRotor = Spot::EntityId::Invalid;

    float GetForwardSpeed() const override;
    float GetIdleRpm() const override { return rotorRpm * 0.2f; }
    float GetRpm() const override { return rotorRpm * m_rotorSpeed; }
    float GetThrottle() const override;
    float GetRotorSpeed() const { return m_rotorSpeed; }
    std::vector<Gauge> DescribeTelemetry() const override;
    std::vector<InputHint> DescribeHints() const override;

protected:
    void OnFixedUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    float m_rotorSpeed = 0.0f;
    float m_mainAngle = 0.0f;
    float m_tailAngle = 0.0f;
};

} // namespace Gameplay
