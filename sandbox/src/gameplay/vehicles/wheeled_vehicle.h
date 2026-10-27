#pragma once

#include "gameplay/vehicles/vehicle.h"

#include <spot.h>

#include <vector>

namespace Gameplay {

class VehicleWheel;

// An engine with a torque curve, an automatic gearbox and a final drive.
struct Drivetrain {
    // In newton meters, at peakTorqueRpm.
    float peakTorque = 280.0f;
    float idleRpm = 900.0f;
    float peakTorqueRpm = 4200.0f;
    float redlineRpm = 6800.0f;
    // What the engine holds back with the throttle closed, in newton meters at the redline.
    float engineBraking = 60.0f;
    float engineInertia = 0.2f;
    std::vector<float> gears = {3.4f, 2.1f, 1.45f, 1.1f, 0.87f};
    float reverseGear = 3.3f;
    float finalDrive = 3.9f;
    // Seconds the clutch is out while shifting.
    float shiftTime = 0.2f;

    void Serialize(Spot::Archive& archive);
};

// A vehicle on wheels, driving on its Rigidbody, simulated as real ones are, with any number of VehicleWheels below
// it. Each wheel is cast to the ground every physics step and holds the body up on a spring and a damper, with
// anti-roll bars between the wheels of each axle. Tires grip by how much they slip, along and across, within the
// friction their load gives them: they slide past their peak, and grip less as they are loaded more. The drivetrain
// turns the driven wheels through an open differential; brakes, with anti-lock, and a handbrake stop them, and the air
// drags. Steered wheels turn around a point on the line of the unsteered ones, so inner ones turn more (Ackermann).
//
// Its origin is on the ground under it, -Z forward. Without a driver, its brakes hold it and its engine stops.
class WheeledVehicle : public Vehicle {
public:
    // Newtons per meter of difference between the wheels of an axle.
    float antiRoll = 12000.0f;
    // How high above the ground the suspension takes the tires' forces into the body, as its links do.
    float rollCenterHeight = 0.35f;

    // The tires' friction coefficient at their peak, and how much less they grip at twice their resting load.
    float grip = 1.05f;
    float loadSensitivity = 0.12f;
    // How much tires slip at their peak grip: along, as a share of the speed, and across, as an angle.
    float peakSlipRatio = 0.1f;
    float peakSlipAngle = Spot::ToRadians(8.0f);
    float rollingResistance = 0.013f;

    // Steering lessens at speed, halved at steerFalloffSpeed (meters per second).
    float steerFalloffSpeed = 22.0f;
    // How fast the wheel turns, full lock per second.
    float steerSpeed = 2.5f;

    Drivetrain drivetrain;
    // In newton meters, for all the wheels by their shares, and the handbrake's on each wheel it holds.
    float brakeTorque = 7000.0f;
    float handbrakeTorque = 2500.0f;
    bool antiLock = true;
    // Half the air's density times its drag coefficient and frontal area.
    float dragCoefficient = 0.42f;

    // Its wheels, in their order below it.
    std::vector<VehicleWheel*> GetWheels() const;
    // The gear engaged: 1 and up forward, -1 reverse.
    int GetGear() const { return m_gear; }
    // From -1, full left, to 1, as the driver has turned the wheel by now.
    float GetSteering() const { return m_steer; }

    float GetForwardSpeed() const override { return m_speed; }
    std::vector<Gauge> DescribeTelemetry() const override;
    float GetIdleRpm() const override { return drivetrain.idleRpm; }
    float GetRpm() const override { return m_rpm; }
    float GetThrottle() const override { return m_throttle; }
    float GetSlip() const override;

protected:
    void OnPossess(Controller& controller) override;
    void OnUnpossess() override;
    void OnStart() override;
    void OnFixedUpdate(float fixedDeltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    // Where the wheel's center rests, from the vehicle.
    Spot::Vector3 GetRest(const VehicleWheel& wheel) const;
    // The total ratio between the engine and the wheels in the gear: negative in reverse, 0 in neutral.
    float GetRatio(int gear) const;
    float GetEngineTorque(float rpm) const;
    // The driven wheels' spin, by their shares of the drive.
    float GetDrivenSpin(const std::vector<VehicleWheel*>& wheels) const;
    // Each wheel's share of the weight at rest, balancing the center of mass along the vehicle.
    std::vector<float> GetRestShares(const std::vector<VehicleWheel*>& wheels) const;
    void UpdateSteering(float deltaTime, const std::vector<VehicleWheel*>& wheels);
    // Shifts as an automatic gearbox does, and works out throttle and brakes from the input.
    void UpdateGearbox(float deltaTime, float& brake, const std::vector<VehicleWheel*>& wheels);
    void Reset();

    // Where the body's mass is centered, from the vehicle, as its box colliders put it.
    Spot::Vector3 m_centerOfMass;
    float m_speed = 0.0f;
    float m_steer = 0.0f;
    float m_throttle = 0.0f;
    float m_rpm = 0.0f;
    int m_gear = 1;
    int m_nextGear = 1;
    float m_shiftLeft = 0.0f;
    // Seconds the parked vehicle has stood still, to put it to sleep.
    float m_still = 0.0f;
};

} // namespace Gameplay
