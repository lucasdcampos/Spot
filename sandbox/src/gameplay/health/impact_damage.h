#pragma once

#include <spot.h>
#include <spot/scene/physics/collision_listener.h>

#include <string>

namespace Gameplay {

// Damages what its body runs into hard enough, more the faster they meet, as a car running over things does: a
// target's receivers decide what that does, whether it hurts, sets it off or throws it down.
class ImpactDamage : public Spot::Component, public Spot::CollisionListener {
public:
    // Meters per second along the contact's normal below which it does nothing.
    float minSpeed = 2.0f;
    // Damage, and newton seconds of push at least, per meter per second.
    float damagePerSpeed = 10.0f;
    float impulsePerSpeed = 80.0f;
    std::string damageType = "Impact";

protected:
    void OnCollisionEnter(const Spot::Collision& collision) override;
    void Serialize(Spot::Archive& archive) override;
};

} // namespace Gameplay
