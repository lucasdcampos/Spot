#pragma once

#include "gameplay/health/health.h"
#include "gameplay/weapons/explosive.h"

#include <spot.h>

namespace Gameplay {

// A vehicle keeps its wreck after death. Damage accumulates without regeneration, including hard collisions.
class VehicleHealth : public Health, public Spot::CollisionListener {
public:
    VehicleHealth();

    BlastSettings blast{.radius = 12.0f, .impulse = 4000.0f, .damage = 600.0f, .shake = 0.08f};
    // Upward speed added by the blast, in meters per second; the off-center impulse also turns the wreck.
    float wreckLaunchSpeed = 6.0f;
    Spot::AssetRef<Spot::Material> burnedMaterial;
    float safeImpactSpeed = 8.0f;
    float damagePerSpeed = 15.0f;

    bool IsDestroyed() const { return m_destroyed; }

protected:
    void OnStart() override;
    void OnCollisionEnter(const Spot::Collision& collision) override;
    void Serialize(Spot::Archive& archive) override;

private:
    void Explode(const DamageInfo& damage);
    void ApplyWreck();

    bool m_destroyed = false;
    float m_savedHealth = -1.0f;
    Spot::SignalConnection m_died;
};

} // namespace Gameplay
