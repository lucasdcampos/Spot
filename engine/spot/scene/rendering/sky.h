#pragma once

#include "spot/core/math/color.h"
#include "spot/render/sky_data.h"
#include "spot/scene/component.h"

namespace Spot {

class DirectionalLight;

// Fills the background of what Renderer3D::DrawScene() draws with an atmosphere lit by the scene's sun: the brightest
// active DirectionalLight, whose disc shows where its light comes from. Turning that light turns the sky with it, from
// a blue day through red sunsets to night once it is below the horizon. The sky lights the scene the same way: the
// sun's light crosses the air, so it dims and reddens near the horizon and goes out below it, and the sky adds ambient
// light of its own:
//
//     auto& sun = scene.CreateEntity("Sun");
//     sun.GetTransform().SetRotation(Spot::Quaternion::LookRotation({-0.4f, -1.0f, -0.6f}));
//     sun.AddComponent<Spot::DirectionalLight>();
//     scene.CreateEntity("Sky").AddComponent<Spot::Sky>();
//
// Once the sun has set, stars come out, turning with the night sky (SetStarRotation()), and a moon rises opposite the
// sun. A Clouds component in the scene adds a layer of clouds the wind carries. Only the first active sky of a scene is
// drawn, and its entity's transform plays no part. See SkyData for what each setting does.
class Sky final : public Component {
public:
    // The light that lights the sky, or null when the scene has no active directional light.
    const DirectionalLight* FindSun() const;

    float GetSunSize() const { return m_sunSize; }
    void SetSunSize(float angularRadius) { m_sunSize = angularRadius; }
    float GetAtmosphereThickness() const { return m_atmosphereThickness; }
    void SetAtmosphereThickness(float thickness) { m_atmosphereThickness = thickness; }
    Color GetSkyTint() const { return m_skyTint; }
    void SetSkyTint(Color tint) { m_skyTint = tint; }
    Color GetGroundColor() const { return m_groundColor; }
    void SetGroundColor(Color color) { m_groundColor = color; }
    Color GetNightColor() const { return m_nightColor; }
    void SetNightColor(Color color) { m_nightColor = color; }
    float GetExposure() const { return m_exposure; }
    void SetExposure(float exposure) { m_exposure = exposure; }
    float GetAmbientIntensity() const { return m_ambientIntensity; }
    void SetAmbientIntensity(float intensity) { m_ambientIntensity = intensity; }
    // Stars and the moon; see SkyNight. The rotation turns the stars around the axis, as the hours of a day do.
    float GetStarBrightness() const { return m_starBrightness; }
    void SetStarBrightness(float brightness) { m_starBrightness = brightness; }
    float GetStarDensity() const { return m_starDensity; }
    void SetStarDensity(float density) { m_starDensity = density; }
    Vector3 GetStarAxis() const { return m_starAxis; }
    void SetStarAxis(Vector3 axis) { m_starAxis = axis; }
    float GetStarRotation() const { return m_starRotation; }
    void SetStarRotation(float radians) { m_starRotation = radians; }
    float GetMoonSize() const { return m_moonSize; }
    void SetMoonSize(float angularRadius) { m_moonSize = angularRadius; }
    float GetMoonBrightness() const { return m_moonBrightness; }
    void SetMoonBrightness(float brightness) { m_moonBrightness = brightness; }

    // The sky as it is now, lit by the scene's sun, if any.
    SkyData GetSkyData() const;

protected:
    void Serialize(Archive& archive) override;

private:
    float m_sunSize = SkyData{}.sunSize;
    float m_atmosphereThickness = SkyData{}.atmosphereThickness;
    Color m_skyTint = SkyData{}.skyTint;
    Color m_groundColor = SkyData{}.groundColor;
    Color m_nightColor = SkyData{}.nightColor;
    float m_exposure = SkyData{}.exposure;
    float m_ambientIntensity = SkyData{}.ambientIntensity;
    float m_starBrightness = SkyNight{}.starBrightness;
    float m_starDensity = SkyNight{}.starDensity;
    Vector3 m_starAxis = SkyNight{}.starAxis;
    float m_starRotation = 0.0f;
    float m_moonSize = SkyNight{}.moonSize;
    float m_moonBrightness = SkyNight{}.moonBrightness;
};

} // namespace Spot
