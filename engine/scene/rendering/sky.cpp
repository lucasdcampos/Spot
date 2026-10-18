#include "scene/rendering/sky.h"

#include "assets/asset_archive.h"
#include "core/archive.h"
#include "core/time.h"
#include "scene/rendering/clouds.h"
#include "scene/rendering/light.h"
#include "scene/scene.h"

#include <algorithm>

namespace Spot {

const DirectionalLight* Sky::FindSun() const {
    const DirectionalLight* sun = nullptr;
    float brightest = -1.0f;
    GetScene().ForEachComponent<DirectionalLight>([&](const DirectionalLight& light) {
        const Color color = light.GetColor();
        const float brightness = std::max({color.r, color.g, color.b}) * light.GetIntensity();
        if (brightness > brightest) {
            brightest = brightness;
            sun = &light;
        }
    });
    return sun;
}

SkyData Sky::GetSkyData() const {
    SkyData sky{.hasSun = false,
                .sunSize = m_sunSize,
                .atmosphereThickness = m_atmosphereThickness,
                .skyTint = m_skyTint,
                .groundColor = m_groundColor,
                .nightColor = m_nightColor,
                .exposure = m_exposure,
                .ambientIntensity = m_ambientIntensity,
                .night = {.starBrightness = m_starBrightness,
                          .starDensity = m_starDensity,
                          .starAxis = m_starAxis,
                          .starRotation = m_starRotation,
                          .time = static_cast<float>(Time::GetTime()),
                          .moonSize = m_moonSize,
                          .moonBrightness = m_moonBrightness}};
    // The first active clouds.
    const Clouds* clouds = nullptr;
    GetScene().ForEachComponent<Clouds>([&clouds](const Clouds& found) { clouds = clouds ? clouds : &found; });
    if (clouds) {
        sky.clouds = clouds->GetSkyClouds();
    }
    if (const DirectionalLight* sun = FindSun()) {
        const LightData light = sun->GetLightData();
        sky.hasSun = true;
        sky.sunDirection = light.direction;
        sky.sunColor = light.color;
        sky.sunIntensity = light.intensity;
        sky.sunCastsShadows = light.castsShadows;
    }
    return sky;
}

void Sky::Serialize(Archive& archive) {
    archive.Field("sunSize", m_sunSize,
                  {.min = 0.0f, .max = ToRadians(10.0f), .angle = true, .tooltip = "The sun's angular radius"});
    archive.Field("atmosphereThickness", m_atmosphereThickness,
                  {.min = 0.0f, .max = 5.0f, .speed = 0.01f, .tooltip = "Thicker air scatters more light"});
    archive.Field("skyTint", m_skyTint);
    archive.Field("groundColor", m_groundColor);
    archive.Field("nightColor", m_nightColor, {.tooltip = "The light left when the sun is down"});
    archive.Field("exposure", m_exposure, {.min = 0.0f, .max = 10.0f, .speed = 0.01f});
    archive.Field("ambientIntensity", m_ambientIntensity,
                  {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "How much the sky lights the scene"});
    archive.Field(
        "starBrightness", m_starBrightness,
        {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "How bright the stars are at night; 0 hides them"});
    archive.Field("starDensity", m_starDensity, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
    archive.Field("starAxis", m_starAxis, {.speed = 0.01f, .tooltip = "What the night sky turns around"});
    archive.Field("starRotation", m_starRotation, {.angle = true, .tooltip = "How far the night sky has turned"});
    archive.Field(
        "moonSize", m_moonSize,
        {.min = 0.0f, .max = ToRadians(10.0f), .angle = true, .tooltip = "The moon's angular radius; 0 hides it"});
    archive.Field("moonBrightness", m_moonBrightness, {.min = 0.0f, .max = 10.0f, .speed = 0.01f});
}

} // namespace Spot
