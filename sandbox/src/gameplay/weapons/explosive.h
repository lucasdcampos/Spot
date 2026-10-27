#pragma once

#include <spot.h>

#include <string>

namespace Gameplay {

// What a blast is: how far it reaches, how hard it hits at its center, less farther and behind cover, and what it
// plays and shakes.
struct BlastSettings {
    float radius = 5.0f;
    // In newton seconds, at the center.
    float impulse = 400.0f;
    float damage = 110.0f;
    std::string damageType = "Explosion";
    // The effect of the EffectLibrary it plays in the air; with ".dust" after it, what it raises from the ground close
    // below; ".water" at the surface of a volume such as water, and ".underwater" under it.
    std::string effect = "explosion";
    // Radians the views close to it shake by, for a blast of 5 meters.
    float shake = 0.05f;

    void Serialize(Spot::Archive& archive);
};

// Blows up its entity, as a rocket's warhead or a barrel: what is within its radius is damaged and pushed away
// (Damage::ApplyRadial()), views around shake (CameraShake::Add()) and its effects play. What sets it off calls
// Explode(), as a Projectile hitting or IgniteOnDamage's fuse running out.
class Explosive : public Spot::Component {
public:
    // Saved as its own fields.
    BlastSettings blast;

    // A blast at the center, passing by what is ignored, caused by the instigator.
    static void Explode(Spot::Scene& scene, Spot::Vector3 center, const BlastSettings& blast,
                        Spot::EntityId ignore = Spot::EntityId::Invalid,
                        Spot::EntityId instigator = Spot::EntityId::Invalid);

    // Destroys its entity in a blast at the point, caused by the instigator.
    void Explode(Spot::Vector3 center, Spot::EntityId instigator = Spot::EntityId::Invalid);
    // As above, where its entity is.
    void Explode();

protected:
    void Serialize(Spot::Archive& archive) override;

private:
    bool m_exploded = false;
};

} // namespace Gameplay
