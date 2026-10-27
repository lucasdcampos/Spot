#pragma once

#include <spot.h>

#include <string>

namespace Gameplay {

// Landing faster than a safe speed hurts its entity, more the faster, up to its Health's whole at a lethal speed.
// Whatever moves the entity tells it each landing (Land()).
class FallDamage : public Spot::Component {
public:
    // Meters per second.
    float safeFallSpeed = 12.0f;
    float lethalFallSpeed = 26.0f;
    std::string damageType = "Fall";

    // Hurts as landing at the speed, in meters per second, does; returns the health it took.
    float Land(float speed);

protected:
    void Serialize(Spot::Archive& archive) override;
};

} // namespace Gameplay
