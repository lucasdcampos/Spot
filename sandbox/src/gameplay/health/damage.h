#pragma once

#include <spot.h>

#include <string>

namespace Gameplay {

// One hurt, from any source to any target.
struct DamageInfo {
    float amount = 0.0f;
    // A name the game chooses, such as "Bullet", "Explosion", "Impact" or "Fall", that receivers may react to.
    std::string type;
    Spot::Vector3 point;
    // In newton seconds: what the damage pushes its target with.
    Spot::Vector3 impulse;
    // Who caused it, such as the player's pawn, and what did it, such as the rocket or the car.
    Spot::EntityId instigator = Spot::EntityId::Invalid;
    Spot::EntityId source = Spot::EntityId::Invalid;
    // Whether the impulse also pushes the body hit; not for a collision, which the physics already pushed.
    bool pushesBody = true;
};

// A component that takes damage: health, a barrel that catches fire, a npc falling as a ragdoll, a character thrown
// back. Damage finds receivers on what it hits, so sources know none of them.
class DamageReceiver {
public:
    virtual ~DamageReceiver() = default;
    virtual void OnDamage(const DamageInfo& damage) = 0;
};

// A blast's damage: the most at its center, less with distance to none at the radius.
struct RadialDamage {
    float radius = 5.0f;
    float amount = 0.0f;
    // In newton seconds, at the center.
    float impulse = 0.0f;
    std::string type = "Explosion";
    Spot::EntityId instigator = Spot::EntityId::Invalid;
    Spot::EntityId source = Spot::EntityId::Invalid;
    // What the blast passes by, such as what exploded.
    Spot::EntityId ignore = Spot::EntityId::Invalid;
    // The share characters behind cover take.
    float cover = 0.35f;
};

// What damage was given to, for whatever reacts to damage done rather than taken, as a hit marker.
struct DamageApplied {
    Spot::EntityId target = Spot::EntityId::Invalid;
    DamageInfo damage;
};

// The damage of a scene, for listening to it all.
class DamageSystem final : public Spot::SceneSystem {
public:
    static DamageSystem& Get(Spot::Scene& scene);

    // Each time damage reached a receiver or pushed a body.
    Spot::Signal<const DamageApplied&> OnDamageApplied;
};

namespace Damage {

// Gives the damage to the receivers of the target or of its nearest ancestor that has some, or of the ragdoll whose
// body it is, and pushes the target's body, unless it is a ragdoll's, which its receivers push. Returns whether
// anything took it.
bool Apply(Spot::Scene& scene, Spot::EntityId target, const DamageInfo& damage);
// Gives each receiver within the radius a share of the damage once, less the farther it is and less to characters
// behind cover, pushing it away from the center, lifted a little; pushes the bodies within the radius alike.
void ApplyRadial(Spot::Scene& scene, Spot::Vector3 center, const RadialDamage& damage);

} // namespace Damage

} // namespace Gameplay
