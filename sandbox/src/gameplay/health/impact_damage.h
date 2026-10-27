#pragma once

#include <spot.h>
#include <spot/scene/physics/collision_listener.h>

#include <string>

namespace Gameplay {

// Damages what its body runs into hard enough, more the faster they meet, as a car running over things does: a
// target's receivers decide what that does, whether it hurts, sets it off or throws it down.
class ImpactDamage : public Spot::Component, public Spot::CollisionListener {
public:
    // Both the closing speed and this body's speed toward the target must reach this, in meters per second.
    float minSpeed = 2.0f;
    // Damage, and newton seconds of push at least, per meter per second.
    float damagePerSpeed = 10.0f;
    float impulsePerSpeed = 80.0f;
    std::string damageType = "Impact";

protected:
    void OnFixedUpdate(float deltaTime) override;
    void OnCollisionEnter(const Spot::Collision& collision) override;
    void Serialize(Spot::Archive& archive) override;

private:
    Spot::Vector3 m_velocity = {};
};

} // namespace Gameplay
