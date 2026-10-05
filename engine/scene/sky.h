#pragma once

#include "graphics/color.h"
#include "graphics/sky_data.h"
#include "scene/component.h"

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
// Only the first active sky of a scene is drawn, and its entity's transform plays no part. See SkyData for what each
// setting does.
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
};

} // namespace Spot
