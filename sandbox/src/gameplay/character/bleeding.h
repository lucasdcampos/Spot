#pragma once

#include "gameplay/effects/decals.h"
#include "gameplay/health/damage.h"

#include <spot.h>

#include <random>
#include <string>

namespace Gameplay {

// Bleeds where its entity is hurt, alive or dead: sprays the exit effect on along the hit, spatters blood on what is
// behind it, as walls and the floor, stretched along the way it flew, and drips it on the ground around. Marks
// (Decals) are never laid on characters, nor on their ragdolls, whose meshes they could not follow.
class Bleeding : public Spot::Component, public DamageReceiver {
public:
    // Thrown across surfaces, its texture running down the way the blood flew.
    DecalStyle spatter;
    // Fallen on surfaces from above.
    DecalStyle drip;
    // The effect of the EffectLibrary that sprays out along the hit.
    std::string exitEffect = "blood.exit";
    // The type of damage that bleeds (see DamageInfo).
    std::string damageType = "Bullet";
    // Rays of spatter thrown each hit, within the cone of this half angle, in radians, around the hit.
    int spatterCount = 2;
    float spatterCone = 0.4f;
    // Meters within which blood reaches behind the hit and below it.
    float spatterReach = 2.2f;
    float dripReach = 3.0f;
    // Drops fallen around the hit each time, as far as the radius, in meters.
    int dripCount = 1;
    float dripRadius = 0.18f;
    // Meters across of the marks, from the smallest to the largest.
    float minSize = 0.04f;
    float maxSize = 0.14f;

protected:
    void OnStart() override;
    void OnDamage(const DamageInfo& damage) override;
    void Serialize(Spot::Archive& archive) override;

private:
    // Lays a mark on the first surface the ray meets that is not a character; false when it meets none. Spatter runs
    // along the ray, and grows and stretches the farther it flew.
    bool Splash(const DecalStyle& style, Spot::Vector3 from, Spot::Vector3 direction, float reach, float size,
                bool thrown);
    bool IsCharacter(const Spot::Entity* entity);
    float Random(float from, float to);

    // Fixed, so the same hits leave the same marks.
    std::mt19937 m_random{4242};
    Spot::SignalConnection m_damage;
};

} // namespace Gameplay
