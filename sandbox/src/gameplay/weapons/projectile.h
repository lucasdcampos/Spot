#pragma once

#include <spot.h>
#include <spot/scene/physics/collision_listener.h>

#include <optional>

namespace Gameplay {

// A shot that flies, as a rocket: where it hits something other than whoever fired it, or a volume such as water
// (Traces), or once its time is up, it is gone, setting off its entity's Explosive if it has one.
class Projectile : public Spot::Component, public Spot::CollisionListener {
public:
    float lifetime = 5.0f;
    // Who fired it, which it passes through and whose its explosion is.
    Spot::EntityId shooter = Spot::EntityId::Invalid;

    // Makes the prefab at the point, flying along the direction at the speed, fired by the shooter; null when the
    // prefab has no Projectile.
    static Projectile* Launch(Spot::Scene& scene, const Spot::AssetRef<Spot::Prefab>& prefab, Spot::Vector3 position,
                              Spot::Vector3 direction, float speed, Spot::EntityId shooter);

    // Ends its flight at the point.
    void Hit(Spot::Vector3 point);

protected:
    void OnStart() override;
    void OnFixedUpdate(float fixedDeltaTime) override;
    void OnCollisionEnter(const Spot::Collision& collision) override;
    void Serialize(Spot::Archive& archive) override;

private:
    bool m_hit = false;
    // Where it was a step ago, to tell when it went into a volume, which has no collider.
    std::optional<Spot::Vector3> m_last;
};

} // namespace Gameplay
