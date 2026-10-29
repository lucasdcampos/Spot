#pragma once

#include <spot.h>

namespace Gameplay {

class Vehicle;

// The AudioSource of its entity loops as the engine of the Vehicle above it while it has a driver: its pitch follows
// the revolutions, and it gets louder as the throttle opens.
class VehicleEngineSound : public Spot::Component {
public:
    // The revolutions a minute its sound was recorded at.
    float recordedRpm = 3000.0f;

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    float m_volume = 1.0f;
};

// The AudioSource of its entity sounds while the driver of the Vehicle above it holds the horn.
class VehicleHornSound : public Spot::Component {
protected:
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
};

// The AudioSource of its entity squeals as the tires of the Vehicle above it slide past their peak grip, and not
// while it barely moves.
class VehicleTireSound : public Spot::Component {
protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;

private:
    float m_volume = 1.0f;
};

} // namespace Gameplay
