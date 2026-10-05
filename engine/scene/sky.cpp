#include "scene/sky.h"

#include "scene/archive.h"
#include "scene/light.h"
#include "scene/scene.h"

#include <algorithm>

namespace Spot {

SkyData Sky::GetSkyData() const {
    SkyData sky{.hasSun = false,
                .sunSize = m_sunSize,
                .atmosphereThickness = m_atmosphereThickness,
                .skyTint = m_skyTint,
                .groundColor = m_groundColor,
                .nightColor = m_nightColor,
                .exposure = m_exposure};

    float brightest = -1.0f;
    GetScene().ForEachComponent<DirectionalLight>([&](const DirectionalLight& light) {
        const Color color = light.GetColor();
        const float brightness = std::max({color.r, color.g, color.b}) * light.GetIntensity();
        if (brightness > brightest) {
            brightest = brightness;
            const LightData data = light.GetLightData();
            sky.hasSun = true;
            sky.sunDirection = data.direction;
            sky.sunColor = data.color;
            sky.sunIntensity = data.intensity;
        }
    });
    return sky;
}

void Sky::Serialize(Archive& archive) {
    archive.Field("sunSize", m_sunSize);
    archive.Field("atmosphereThickness", m_atmosphereThickness);
    archive.Field("skyTint", m_skyTint);
    archive.Field("groundColor", m_groundColor);
    archive.Field("nightColor", m_nightColor);
    archive.Field("exposure", m_exposure);
}

} // namespace Spot
