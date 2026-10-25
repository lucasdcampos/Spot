#pragma once

#include <spot.h>
#include <spot/scene/physics/collision_listener.h>

#include <array>
#include <vector>

// How the driver works a car. Drive goes from -1 to 1: forward accelerates and back brakes, and once the car has
// stopped, back reverses and forward brakes, as with an automatic gearbox. Steer goes from -1, full left, to 1.
struct CarControls {
    float drive = 0.0f;
    float steer = 0.0f;
    bool handbrake = false;
};

// A car that drives on its Rigidbody, simulated as real ones are. Each wheel is cast to the ground every physics step
// and holds the body up on a spring and a damper, with anti-roll bars between the wheels of each axle. Tires grip by
// how much they slip, along and across, within the friction their load gives them: they slide past their peak, and
// grip less as they are loaded more. An engine with a torque curve turns the driven wheels through an automatic
// gearbox, a final drive and an open differential; brakes, with anti-lock, and a handbrake on the rear wheels stop
// them, and the air drags. Wheels spin and steer on their own, with the inner wheel turning more (Ackermann).
//
// The car's origin is on the ground under it, -Z forward. Its wheel entities, children of it, only show where the
// wheels are; the wheels' positions are frontWheel and rearWheel, those on the left mirrored. Without a driver, its
// brakes hold it and its engine stops. The driver's controls come from SetControls().
class Car final : public Spot::Component, public Spot::CollisionListener {
public:
    enum class Drive { Front, Rear, All };

    // The right wheels' centers at rest, from the car; the left ones mirror them.
    Spot::Vector3 frontWheel = {0.78f, 0.33f, -1.3f};
    Spot::Vector3 rearWheel = {0.78f, 0.33f, 1.3f};
    float wheelRadius = 0.33f;
    float wheelWidth = 0.22f;
    // The wheel's own inertia, in kg m²; the engine's adds to that of the driven ones in gear.
    float wheelInertia = 1.2f;
    // Where the driver sits, from the car.
    Spot::Vector3 seat = {-0.38f, 0.9f, 0.2f};

    // In newtons per meter, and per meter per second.
    float springRate = 32000.0f;
    float damping = 2800.0f;
    // How far each wheel moves up from rest, and down.
    float bumpTravel = 0.12f;
    float droopTravel = 0.16f;
    // Newtons per meter of difference between the two wheels of an axle.
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

    // The front wheels' steering at rest, which lessens at speed, halved at steerFalloffSpeed (meters per second).
    float maxSteerAngle = Spot::ToRadians(34.0f);
    float steerFalloffSpeed = 22.0f;
    // How fast the wheel turns, full lock per second.
    float steerSpeed = 2.5f;

    Drive drive = Drive::Rear;
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

    // In newton meters for all four wheels, the share the front ones take, and the handbrake's on each rear wheel.
    float brakeTorque = 7000.0f;
    float brakeBias = 0.65f;
    float handbrakeTorque = 2500.0f;
    bool antiLock = true;
    // Half the air's density times the car's drag coefficient and frontal area.
    float dragCoefficient = 0.42f;

    Spot::EntityId frontLeft = Spot::EntityId::Invalid;
    Spot::EntityId frontRight = Spot::EntityId::Invalid;
    Spot::EntityId rearLeft = Spot::EntityId::Invalid;
    Spot::EntityId rearRight = Spot::EntityId::Invalid;
    // Inactive until someone drives: an entity with the Camera that follows the car.
    Spot::EntityId camera = Spot::EntityId::Invalid;
    // AudioSources: the engine's loop, whose pitch follows the revolutions, and the tires' squeal as they slide.
    Spot::EntityId engineSound = Spot::EntityId::Invalid;
    Spot::EntityId tireSound = Spot::EntityId::Invalid;

    void SetControls(const CarControls& controls) { m_controls = controls; }
    const CarControls& GetControls() const { return m_controls; }
    // Who drives it: driving starts the engine and shows the car's camera.
    Spot::EntityId GetDriver() const { return m_driver; }
    bool HasDriver() const { return m_driver != Spot::EntityId::Invalid; }
    void SetDriver(Spot::EntityId driver);

    // Meters per second forward, negative backing up.
    float GetSpeed() const { return m_speed; }
    // The gear engaged: 1 and up forward, -1 reverse.
    int GetGear() const { return m_gear; }
    float GetRpm() const { return m_rpm; }
    float GetThrottle() const { return m_throttle; }
    float GetSteering() const { return m_steer; }
    // Wheels in order front left, front right, rear left, rear right.
    bool IsWheelGrounded(int wheel) const { return m_wheels[static_cast<std::size_t>(wheel)].grounded; }
    // How far past its peak grip the tire slides: 0 gripping, 1 at its peak, more sliding.
    float GetWheelSlip(int wheel) const { return m_wheels[static_cast<std::size_t>(wheel)].slip; }
    // Radians per second, positive rolling forward.
    float GetWheelSpin(int wheel) const { return m_wheels[static_cast<std::size_t>(wheel)].spin; }
    // The world position where the car's driver sits.
    Spot::Vector3 GetSeatPosition() const;

protected:
    void OnStart() override;
    void OnDisable() override;
    void OnCollisionEnter(const Spot::Collision& collision) override;
    void OnFixedUpdate(float fixedDeltaTime) override;
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    struct Wheel {
        bool front = false;
        bool left = false;
        // How far the wheel is up from rest, positive compressed, and how far down it reaches in the air.
        float compression = 0.0f;
        bool grounded = false;
        float load = 0.0f;
        float steer = 0.0f;
        float spin = 0.0f;
        float angle = 0.0f;
        float slip = 0.0f;
    };
    // What the ground under a wheel is, found this step.
    struct Contact {
        Spot::Vector3 point;
        Spot::Vector3 normal;
        Spot::EntityId entity = Spot::EntityId::Invalid;
    };

    Spot::Vector3 GetWheelRest(const Wheel& wheel) const;
    bool IsDriven(const Wheel& wheel) const;
    // The total ratio between the engine and the wheels in the gear: negative in reverse, 0 in neutral.
    float GetRatio(int gear) const;
    float GetEngineTorque(float rpm) const;
    void UpdateSteering(float deltaTime);
    // Shifts as an automatic gearbox does, and works out throttle and brakes from the controls.
    void UpdateGearbox(float deltaTime, float& brake);
    void UpdateSounds();
    Spot::AudioSource* GetSound(Spot::EntityId entity) const;

    CarControls m_controls;
    Spot::EntityId m_driver = Spot::EntityId::Invalid;
    std::array<Wheel, 4> m_wheels;
    // Where the body's mass is centered, from the car, as its box colliders put it.
    Spot::Vector3 m_centerOfMass;
    float m_speed = 0.0f;
    float m_steer = 0.0f;
    float m_throttle = 0.0f;
    float m_rpm = 0.0f;
    int m_gear = 1;
    int m_nextGear = 1;
    float m_shiftLeft = 0.0f;
    // Seconds the parked car has stood still, to put it to sleep.
    float m_still = 0.0f;
    float m_engineVolume = 1.0f;
    float m_tireVolume = 1.0f;
};
