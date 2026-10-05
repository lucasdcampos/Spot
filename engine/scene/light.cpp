#include "scene/light.h"

#include "scene/archive.h"
#include "scene/entity.h"
#include "scene/transform.h"

namespace Spot {

namespace {

Vector3 GetForward(const Entity& entity) {
    return entity.GetTransform().GetWorldRotation() * Vector3{0.0f, 0.0f, -1.0f};
}

} // namespace

void Light::Serialize(Archive& archive) {
    archive.Field("color", m_color);
    archive.Field("intensity", m_intensity);
}

LightData Light::GetBaseLightData(LightType type) const {
    return {.type = type, .color = m_color, .intensity = m_intensity};
}

LightData AmbientLight::GetLightData() const {
    return GetBaseLightData(LightType::Ambient);
}

LightData DirectionalLight::GetLightData() const {
    LightData light = GetBaseLightData(LightType::Directional);
    light.direction = GetForward(GetEntity());
    return light;
}

LightData PointLight::GetLightData() const {
    LightData light = GetBaseLightData(LightType::Point);
    light.position = GetEntity().GetTransform().GetWorldPosition();
    light.range = m_range;
    return light;
}

void PointLight::Serialize(Archive& archive) {
    Light::Serialize(archive);
    archive.Field("range", m_range);
}

LightData SpotLight::GetLightData() const {
    LightData light = GetBaseLightData(LightType::Spot);
    light.position = GetEntity().GetTransform().GetWorldPosition();
    light.direction = GetForward(GetEntity());
    light.range = m_range;
    light.innerConeAngle = m_innerConeAngle;
    light.outerConeAngle = m_outerConeAngle;
    return light;
}

void SpotLight::Serialize(Archive& archive) {
    Light::Serialize(archive);
    archive.Field("range", m_range);
    archive.Field("innerConeAngle", m_innerConeAngle);
    archive.Field("outerConeAngle", m_outerConeAngle);
}

} // namespace Spot
