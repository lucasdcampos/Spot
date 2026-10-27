#pragma once

#include <spot.h>

namespace Gameplay {

// The light of a gun firing or something blowing up: a point light that fades out over its lifetime, quickly at first
// as fire dies down, and flickers as flames do, after which its entity goes. What the burst looks like is up to the
// particles beside it (see EffectLibrary).
class Flash : public Spot::Component {
public:
    // Seconds.
    float lifetime = 0.06f;
    // How bright the light starts, which multiplies its color.
    float intensity = 8.0f;
    Spot::Color color = {1.0f, 0.7f, 0.3f, 1.0f};
    // The share of its brightness it flickers by, from 0 for a steady light.
    float flicker = 0.0f;

    // A flash at the position, its light reaching the range.
    static Spot::Entity& Spawn(Spot::Scene& scene, Spot::Vector3 position, Spot::Color color, float range,
                               float intensity, float lifetime, float flicker = 0.0f);

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    void SetStrength(float strength);

    float m_age = 0.0f;
};

} // namespace Gameplay
