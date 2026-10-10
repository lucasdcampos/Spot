#pragma once

#include <spot.h>

// A burst of light where a gun fires or something blows up: a bright light and a ball that glows through bloom, both
// fading out over its lifetime, after which its entity goes. Until particles (M13) give these a look of their own.
class Flash final : public Spot::Component {
public:
    // Seconds.
    float lifetime = 0.06f;
    // How bright the light and the ball start, which multiplies their color.
    float intensity = 8.0f;
    Spot::Color color = {1.0f, 0.7f, 0.3f, 1.0f};

    // A flash at the position, its ball the size across and its light reaching the range.
    static Spot::Entity& Spawn(Spot::Scene& scene, Spot::Vector3 position, Spot::Color color, float size, float range,
                               float intensity, float lifetime);

protected:
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    float m_age = 0.0f;
};
