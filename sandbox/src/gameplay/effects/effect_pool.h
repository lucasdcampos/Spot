#pragma once

#include <spot.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Gameplay {

// One burst of particles of an effect, as authored: what the particles are and how many there are of them.
struct ParticleLayer {
    Spot::ParticleSettings settings;
    Spot::AssetRef<Spot::Texture> texture;
    std::uint32_t count = 12;
    // Shaded by the light the scene has (EffectPool::SetLight()), as smoke, dust and spray are, rather than glowing
    // on their own, as fire and sparks do.
    bool lit = false;
    // Multiplies the size of the effect played, for a layer that is a smaller part of it.
    float scale = 1.0f;
    // Meters along the effect's direction it bursts from, as dust just off a wall.
    float offset = 0.0f;

    void Serialize(Spot::Archive& archive);
};

// The emitters of a scene's effects: a fixed number of entities, each reused once its burst is over, so effects never
// make entities as they play. When all are busy, a burst is dropped.
class EffectPool final : public Spot::SceneSystem {
public:
    static constexpr std::size_t MaxEmitters = 128;

    EffectPool();

    static EffectPool& Get(Spot::Scene& scene);

    // Bursts the layer from the position along the direction, its size scaled; the count, unless 0, replaces the
    // layer's. A beam length spreads the particles along that much of the direction.
    Spot::ParticleSystem* Spawn(const ParticleLayer& layer, Spot::Vector3 position,
                                Spot::Vector3 direction = {0.0f, 1.0f, 0.0f}, float scale = 1.0f,
                                std::uint32_t count = 0, float beamLength = 0.0f);
    // How much light falls on lit layers, from 0 in the dark to 1, the default, in full light. What lights the scene,
    // such as a day cycle, sets it.
    void SetLight(float light) { m_light = light; }
    float GetLight() const { return m_light; }
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
    float m_light = 1.0f;
};

} // namespace Gameplay
