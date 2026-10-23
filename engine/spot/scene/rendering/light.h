#pragma once

#include "spot/core/color.h"
#include "spot/graphics/light_data.h"
#include "spot/scene/component.h"

namespace Spot {

// Lights what Renderer3D::DrawScene() draws while it is active, from its entity's world transform. Lights that shine
// in a direction shine down the entity's forward axis, -Z, like cameras look:
//
//     auto& sun = scene.CreateEntity("Sun");
//     sun.GetTransform().SetRotation(Spot::Quaternion::LookRotation({-0.4f, -1.0f, -0.6f}));
//     sun.AddComponent<Spot::DirectionalLight>();
//     scene.CreateEntity("Sky").AddComponent<Spot::AmbientLight>(Spot::Color{0.25f, 0.25f, 0.3f}, 1.0f);
//
// A scene without lights is black.
class Light : public Component {
public:
    Color GetColor() const { return m_color; }
    void SetColor(Color color) { m_color = color; }
    // Multiplies the color, in the units LightData::intensity describes.
    float GetIntensity() const { return m_intensity; }
    void SetIntensity(float intensity) { m_intensity = intensity; }
    // Whether what the scene draws blocks the light from what is behind it; see LightData::castsShadows. Directional
    // lights cast shadows unless told not to, and point and spot lights, whose shadows cost more, only when told to.
    // Ambient lights never do.
    bool IsCastingShadows() const { return m_castingShadows; }
    void SetCastingShadows(bool castingShadows) { m_castingShadows = castingShadows; }

    // The light as it is now, placed by its entity's world transform.
    virtual LightData GetLightData() const = 0;

protected:
    Light(Color color, float intensity, bool castingShadows = false)
        : m_color(color), m_intensity(intensity), m_castingShadows(castingShadows) {}

    void Serialize(Archive& archive) override;

    // The light's color and intensity, of the type.
    LightData GetBaseLightData(LightType type) const;

private:
    Color m_color;
    float m_intensity;
    bool m_castingShadows;
};

// Light that reaches every surface evenly from all directions, so surfaces facing away from the other lights do not
// go black. The ambient lights of a scene add up, and its entity's transform plays no part.
class AmbientLight final : public Light {
public:
    explicit AmbientLight(Color color = Color::White, float intensity = 0.25f) : Light(color, intensity) {}

    LightData GetLightData() const override;
};

// Light that shines down the entity's forward axis from infinitely far away, like the sun.
class DirectionalLight final : public Light {
public:
    explicit DirectionalLight(Color color = Color::White, float intensity = 1.0f) : Light(color, intensity, true) {}

    LightData GetLightData() const override;
};

// Light that shines in every direction from the entity's position, falling off with the square of the distance until
// its range. A bulb lighting a room a few units across has an intensity of about 10.
class PointLight final : public Light {
public:
    explicit PointLight(Color color = Color::White, float intensity = 10.0f, float range = 10.0f)
        : Light(color, intensity), m_range(range) {}

    // The distance at which it stops lighting anything.
    float GetRange() const { return m_range; }
    void SetRange(float range) { m_range = range; }

    LightData GetLightData() const override;

protected:
    void Serialize(Archive& archive) override;

private:
    float m_range;
};

// Light that shines in a cone down the entity's forward axis from its position, like a flashlight, fading out by its
// range and toward the edge of the cone.
class SpotLight final : public Light {
public:
    explicit SpotLight(Color color = Color::White, float intensity = 10.0f, float range = 10.0f)
        : Light(color, intensity), m_range(range) {}

    // The distance at which it stops lighting anything.
    float GetRange() const { return m_range; }
    void SetRange(float range) { m_range = range; }

    // The angles from the center of the cone, in radians: fully lit inside the inner one and fading to nothing at
    // the outer one.
    float GetInnerConeAngle() const { return m_innerConeAngle; }
    void SetInnerConeAngle(float angle) { m_innerConeAngle = angle; }
    float GetOuterConeAngle() const { return m_outerConeAngle; }
    void SetOuterConeAngle(float angle) { m_outerConeAngle = angle; }

    LightData GetLightData() const override;

protected:
    void Serialize(Archive& archive) override;

private:
    float m_range;
    float m_innerConeAngle = LightData{}.innerConeAngle;
    float m_outerConeAngle = LightData{}.outerConeAngle;
};

} // namespace Spot
