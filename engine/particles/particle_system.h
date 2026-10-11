#pragma once

#include "assets/asset_ref.h"
#include "particles/particle_simulation.h"
#include "scene/component.h"
#include "scene/scene_system.h"

namespace Spot {
class Texture;

class ParticleSystem final : public Component {
public:
    const ParticleSettings& GetSettings() const { return m_simulation.GetSettings(); }
    void SetSettings(ParticleSettings settings) { m_simulation.SetSettings(std::move(settings)); }
    const ParticleSimulation& GetSimulation() const { return m_simulation; }
    std::span<const Particle> GetParticles() const { return m_simulation.GetParticles(); }
    const AssetRef<Texture>& GetTexture() const { return m_texture; }
    void SetTexture(AssetRef<Texture> texture) { m_texture = std::move(texture); }
    void Play();
    void Restart();
    void Stop(bool clear = false) { m_simulation.Stop(clear); }
    void Emit(std::uint32_t count);
    void Simulate(float seconds);
    void Seek(float seconds);

protected:
    void OnEnable() override;
    void Serialize(Archive& archive) override;

private:
    void EnsureSystem();
    ParticleSimulation m_simulation;
    AssetRef<Texture> m_texture;
    bool m_initialized = false;
};

class ParticleSceneSystem final : public SceneSystem {
protected:
    void OnUpdate(float deltaTime) override;
};
} // namespace Spot
