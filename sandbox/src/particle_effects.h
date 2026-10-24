#pragma once

#include "sound_bank.h"
#include <spot.h>
#include <spot/scene/physics/collision_listener.h>

#include <optional>

struct ParticleEffect {
    Spot::ParticleSettings settings;
    Spot::AssetRef<Spot::Texture> texture;
    std::uint32_t count = 12;
    // Shaded by the daylight, as smoke, dust and spray are, rather than glowing on its own, as fire and sparks do.
    bool lit = false;
    void Serialize(Spot::Archive& archive);
};

// Authored effect definitions on the scene, shared by weapons, explosions, water and physics interactions.
class ParticleEffects final : public Spot::Component {
public:
    ParticleEffects();
    // A shot: a star of flame at the muzzle, tongues of it thrown forward and a puff of smoke left behind.
    ParticleEffect muzzleFlash, muzzleFlame, muzzleSmoke;
    ParticleEffect sparks, concreteDust, woodDust;
    // An explosion as it unfolds: a blinding flash, a fireball rolling up into black smoke, glowing embers and debris
    // thrown out, and dust kicked up from the ground below.
    ParticleEffect blastFlash, fireball, explosionSmoke, embers, explosionDebris, groundDust;
    // Water struck: droplets thrown up, a spout of spray, mist hanging over it and rings spreading on the surface,
    // and from blasts a column of white water that rains back down.
    ParticleEffect splashDroplets, splashSpray, splashMist, ripples, waterPlume;
    ParticleEffect beam, tracer;
    float beamEmissionRate = 240;
    static ParticleEffects* Find(Spot::Scene& scene);
    // The size scales the flash, as bigger guns make bigger ones.
    static void Muzzle(Spot::Scene& scene, Spot::Vector3 position, Spot::Vector3 direction, float size = 1);
    // A streak of light flying from the muzzle to where a shot went, gone as it gets there.
    static void Tracer(Spot::Scene& scene, Spot::Vector3 from, Spot::Vector3 to);
    static void Impact(Spot::Scene& scene, Spot::Vector3 position, Spot::Vector3 normal, SurfaceKind surface);
    // Where a blast goes off: in the open it burns and smokes; on water it only flashes, as the water it throws up
    // takes the rest (see Splash()); under water nothing shows but the water.
    enum class Blast { InAir, OnWater, UnderWater };
    // A blast of the radius, with dust kicked up where the ground below is, if it is close enough.
    static void Explosion(Spot::Scene& scene, Spot::Vector3 position, float radius, Blast blast = Blast::InAir,
                          std::optional<Spot::Vector3> ground = std::nullopt);
    // Water struck at a point of its surface, as hard as Water::Splash() says.
    static void Splash(Spot::Scene& scene, Spot::Vector3 position, float strength);
    static void Beam(Spot::Scene& scene, Spot::Vector3 from, Spot::Vector3 to, float deltaTime,
                     float& emissionRemainder);

protected:
    void Serialize(Spot::Archive& archive) override;
};

// A fixed-size pool owns transient effect entities by id; expired emitters become reusable slots.
class ParticleEffectPool final : public Spot::SceneSystem {
public:
    static constexpr std::size_t MaxEmitters = 128;
    ParticleEffectPool();
    Spot::ParticleSystem* Spawn(const ParticleEffect& effect, Spot::Vector3 position,
                                Spot::Vector3 direction = {0, 1, 0}, float scale = 1, std::uint32_t count = 0,
                                float beamLength = 0);
    std::size_t GetEmitterCount() const { return m_emitters.size(); }
    std::uint64_t GetDroppedCount() const { return m_dropped; }

protected:
    void OnLateUpdate(float deltaTime) override;

private:
    struct Slot {
        Spot::EntityId entity = Spot::EntityId::Invalid;
        Spot::ParticleSettings settings;
    };
    std::vector<Slot> m_emitters;
    std::uint64_t m_sequence = 0;
    std::uint64_t m_dropped = 0;
};

class ImpactParticles final : public Spot::Component, public Spot::CollisionListener {
public:
    float minSpeed = 1.5f;

protected:
    void OnStart() override;
    void OnCollisionEnter(const Spot::Collision& collision) override;
    void Serialize(Spot::Archive& archive) override;

private:
    Spot::InvokeId m_quiet = Spot::InvokeId::Invalid;
};
