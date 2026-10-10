#pragma once

#include <spot.h>

// Marks a prefab for the sandbox's spawn catalog. Its other components define what spawning creates.
class Spawnable final : public Spot::Component {
public:
    enum class Category { Prop, Lamp, Weapon };

    Category category = Category::Prop;
    int order = 100;
    // Distance from a hit surface to the prefab's origin, in meters.
    float clearance = 0.55f;

protected:
    void Serialize(Spot::Archive& archive) override;
};
