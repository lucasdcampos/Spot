#pragma once

#include <spot.h>

namespace Gameplay {

class Vehicle;

// Each sound is worked out where its vehicle is simulated, its driver's machine while a player drives it, and the
// other machines play what they are shown.

// The AudioSource of its entity loops as the engine of the Vehicle above it while it has a driver: its pitch follows
// the revolutions, and it gets louder as the throttle opens.
class VehicleEngineSound : public Spot::Component, public Spot::Replicated {
public:
    // The revolutions a minute its sound was recorded at.
    float recordedRpm = 3000.0f;

    void Replicate(Spot::Archive& archive) override;
    bool IsDecidedByOwner() const override { return true; }

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    float m_volume = 1.0f;
    bool m_running = false;
    float m_pitch = 1.0f;
    // From 0 idling to 1 at full throttle.
    float m_load = 0.0f;
};

// The AudioSource of its entity sounds while the driver of the Vehicle above it holds the horn.
class VehicleHornSound : public Spot::Component, public Spot::Replicated {
public:
    void Replicate(Spot::Archive& archive) override;
    bool IsDecidedByOwner() const override { return true; }

protected:
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;

private:
    bool m_sounding = false;
};

// The AudioSource of its entity squeals as the tires of the Vehicle above it slide past their peak grip, and not
// while it barely moves.
class VehicleTireSound : public Spot::Component, public Spot::Replicated {
public:
    void Replicate(Spot::Archive& archive) override;
    bool IsDecidedByOwner() const override { return true; }

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;

private:
    float m_volume = 1.0f;
    float m_squeal = 0.0f;
};

} // namespace Gameplay
