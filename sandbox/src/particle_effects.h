#pragma once

#include "sound_bank.h"
#include <spot.h>

struct ParticleEffect {
    Spot::ParticleSettings settings;
    Spot::AssetRef<Spot::Texture> texture;
    std::uint32_t count = 12;
    void Serialize(Spot::Archive& archive);
};

// Authored effect definitions on the scene, shared by weapons and physics interactions.
class ParticleEffects final : public Spot::Component {
public:
    ParticleEffects();
    ParticleEffect muzzle, sparks, concreteDust, woodDust, fire, smoke, debris, beam, tracer;
    float beamEmissionRate = 240;
    static ParticleEffects* Find(Spot::Scene& scene);
    static void Muzzle(Spot::Scene& scene, Spot::Vector3 position, Spot::Vector3 direction);
    // A streak of light flying from the muzzle to where a shot went, gone as it gets there.
    static void Tracer(Spot::Scene& scene, Spot::Vector3 from, Spot::Vector3 to);
    static void Impact(Spot::Scene& scene, Spot::Vector3 position, Spot::Vector3 normal, SurfaceKind surface);
    static void Explosion(Spot::Scene& scene, Spot::Vector3 position, float radius);
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

class ImpactParticles final : public Spot::Component {
public:
    float minSpeed = 1.5f;

protected:
    void OnStart() override;
    void OnCollisionEnter(const Spot::Collision& collision) override;
    void Serialize(Spot::Archive& archive) override;

private:
    Spot::InvokeId m_quiet = Spot::InvokeId::Invalid;
};
