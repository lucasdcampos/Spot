#include "scene/sky.h"

#include "scene/archive.h"
#include "scene/light.h"
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
    }
    return sky;
}

void Sky::Serialize(Archive& archive) {
    archive.Field("sunSize", m_sunSize);
    archive.Field("atmosphereThickness", m_atmosphereThickness);
    archive.Field("skyTint", m_skyTint);
    archive.Field("groundColor", m_groundColor);
    archive.Field("nightColor", m_nightColor);
    archive.Field("exposure", m_exposure);
    archive.Field("ambientIntensity", m_ambientIntensity);
}

} // namespace Spot
