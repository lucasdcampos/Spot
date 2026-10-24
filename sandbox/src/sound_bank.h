#pragma once

#include <spot.h>
#include <spot/scene/physics/collision_listener.h>

#include <cstdint>
#include <vector>

// What an entity is made of, as its sounds tell: how it sounds when hit, and when walked on.
enum class SurfaceKind { Concrete, Wood, Metal };

// The surface of its entity. Entities without one are concrete, as the level is.
class Surface final : public Spot::Component {
public:
    SurfaceKind kind = SurfaceKind::Concrete;

    // The entity's surface; concrete for a null entity or one without a Surface.
    static SurfaceKind Of(const Spot::Entity* entity);

protected:
    void Serialize(Spot::Archive& archive) override;
};

// How a surface sounds.
struct SurfaceSounds {
    Spot::AssetRef<Spot::Sound> impact;
    // Taken in turn, so steps do not all sound the same.
    std::vector<Spot::AssetRef<Spot::Sound>> footsteps;

    void Serialize(Spot::Archive& archive);
};

// The sounds shared by the scene: those of each surface, by SurfaceKind, explosions, splashes in water and the
// interface's click.
// Spawnable prefabs select their surface; weapons carry their own fire and reload sounds.
class SoundBank final : public Spot::Component {
public:
    std::vector<SurfaceSounds> surfaces;
    Spot::AssetRef<Spot::Sound> explosion;
    Spot::AssetRef<Spot::Sound> smallSplash;
    Spot::AssetRef<Spot::Sound> largeSplash;
    Spot::AssetRef<Spot::Sound> click;

    // The scene's bank; null when it has none.
    static SoundBank* Find(Spot::Scene& scene);
    // Null when the bank has no sounds for the surface.
    const SurfaceSounds* GetSurface(SurfaceKind kind) const;
    // Plays the click on the Interface bus, heard everywhere and also while the scenes are paused, as menus are.
    static void PlayClick(Spot::Scene& scene);

protected:
    void Serialize(Spot::Archive& archive) override;
};

// Sounds its entity's surface where it hits something, louder the faster they meet. A short pause after each keeps
// a prop rattling against another from flooding the mixer.
class ImpactSounds final : public Spot::Component, public Spot::CollisionListener {
public:
    // Meters per second along the contact's normal: quieter impacts make no sound, and faster ones play at full volume.
    float minSpeed = 1.0f;
    float fullSpeed = 8.0f;

protected:
    void OnStart() override;
    void OnCollisionEnter(const Spot::Collision& collision) override;
    void Serialize(Spot::Archive& archive) override;

private:
    Spot::InvokeId m_quiet = Spot::InvokeId::Invalid;
    std::uint32_t m_random = 0x2545f491u;
};
