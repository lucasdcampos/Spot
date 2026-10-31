#pragma once

#include <spot.h>
#include <spot/scene/physics/collision_listener.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Gameplay {

// What a kind of surface is like: how it sounds hit and walked on, and the effect hitting it plays.
struct SurfaceDefinition {
    // Such as "Wood".
    std::string name;
    Spot::AssetRef<Spot::Sound> impact;
    // Taken in turn, so steps do not all sound the same.
    std::vector<Spot::AssetRef<Spot::Sound>> footsteps;
    // The name of the effect of the EffectLibrary that a hit plays, as "impact.wood".
    std::string impactEffect;

    void Serialize(Spot::Archive& archive);
};

// The kinds of surfaces of a scene, in order. The first is that of whatever has no Surface, as the level's ground; a
// new kind, such as glass, is a new entry.
class SurfaceLibrary : public Spot::Component {
public:
    std::vector<SurfaceDefinition> surfaces;

    // The scene's first enabled library; null when it has none.
    static SurfaceLibrary* Find(Spot::Scene& scene);
    // The surface of the entity, by its Surface, its nearest ancestor's or its ragdoll's, or else the first; null
    // without a library, or one without that surface.
    static const SurfaceDefinition* Of(Spot::Scene& scene, const Spot::Entity* entity);

    const SurfaceDefinition* Get(std::size_t index) const;
    const SurfaceDefinition* Get(std::string_view name) const;

protected:
    void Serialize(Spot::Archive& archive) override;
};

// What its entity is made of: one of the kinds of the scene's SurfaceLibrary.
class Surface : public Spot::Component {
public:
    // The index of its kind in the SurfaceLibrary.
    int kind = 0;

protected:
    void Serialize(Spot::Archive& archive) override;
};

// Sounds its entity's surface where it hits something, louder the faster they meet. A short pause after each keeps
// a prop rattling against another from flooding the mixer.
class ImpactSounds : public Spot::Component, public Spot::CollisionListener {
public:
    // Meters per second along the contact's normal: quieter impacts make no sound, and faster ones play at full volume.
    float minSpeed = 1.0f;
    float fullSpeed = 8.0f;

    // Sounds an impact at that speed there, unless one just did.
    void Play(float speed, Spot::Vector3 where);

protected:
    void OnStart() override;
    void OnCollisionEnter(const Spot::Collision& collision) override;
    void Serialize(Spot::Archive& archive) override;

private:
    Spot::InvokeId m_quiet = Spot::InvokeId::Invalid;
    std::uint32_t m_random = 0x2545f491u;
};

// Plays its entity's surface's impact effect where it hits something fast enough, with the same pause as ImpactSounds.
class ImpactParticles : public Spot::Component, public Spot::CollisionListener {
public:
    // Meters per second along the contact's normal.
    float minSpeed = 1.5f;

    // Bursts at an impact at that speed there, unless one just did.
    void Play(float speed, Spot::Vector3 where, Spot::Vector3 normal);

protected:
    void OnStart() override;
    void OnCollisionEnter(const Spot::Collision& collision) override;
    void Serialize(Spot::Archive& archive) override;

private:
    Spot::InvokeId m_quiet = Spot::InvokeId::Invalid;
};

} // namespace Gameplay
