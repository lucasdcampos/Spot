#pragma once

#include <spot.h>

namespace Gameplay {

// A wheel of a WheeledVehicle, on an entity of its own below it, placed where the wheel's center rests: its tire, its
// suspension, how far it steers and what share of the drive and the brakes it takes. Its visual, a child, moves up and
// down with the suspension, steers and spins; the wheel's own entity stays where it rests.
class VehicleWheel : public Spot::Component {
public:
    float radius = 0.33f;
    float width = 0.22f;
    // The wheel's own inertia, in kg m²; the engine's adds to that of driven wheels in gear.
    float inertia = 1.2f;
    // In newtons per meter, and per meter per second.
    float springRate = 32000.0f;
    float damping = 2800.0f;
    // How far it moves up from rest, and down.
    float bumpTravel = 0.12f;
    float droopTravel = 0.16f;
    // How far it steers at full lock while standing, in radians, positive turning the vehicle the way it is steered;
    // 0 for a wheel that does not steer, negative for one that steers the other way.
    float maxSteer = 0.0f;
    // Its shares of the engine's drive and of the brakes' torque, out of all the wheels'.
    float driveShare = 0.0f;
    float brakeShare = 0.25f;
    // Whether the handbrake holds it.
    bool handbrake = false;
    // The wheels of the same axle share an anti-roll bar.
    int axle = 0;
    // The entity that shows it, which it moves; none when unset.
    Spot::EntityId visual = Spot::EntityId::Invalid;

    bool IsGrounded() const { return m_grounded; }
    // How far it is up from rest, positive compressed.
    float GetCompression() const { return m_compression; }
    // Newtons it bears.
    float GetLoad() const { return m_load; }
    // Radians it steers now, around the vehicle's up, positive to the left.
    float GetSteer() const { return m_steer; }
    // Radians per second, positive rolling forward.
    float GetSpin() const { return m_spin; }
    // How far past its peak grip the tire slides: 0 gripping, 1 at its peak, more sliding.
    float GetSlip() const { return m_slip; }

protected:
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    friend class WheeledVehicle;

    float m_compression = 0.0f;
    bool m_grounded = false;
    float m_load = 0.0f;
    float m_steer = 0.0f;
    float m_spin = 0.0f;
    float m_angle = 0.0f;
    float m_slip = 0.0f;
    // Whether it is on the vehicle's left, whose wheels are the right ones turned around.
    bool m_left = false;
};

} // namespace Gameplay
