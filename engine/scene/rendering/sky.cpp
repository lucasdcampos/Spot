#include "scene/rendering/sky.h"

#include "assets/asset_archive.h"
#include "core/archive.h"
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
                .ambientIntensity = m_ambientIntensity};
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
}

} // namespace Spot
