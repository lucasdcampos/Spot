#include "scene/rendering/light.h"

#include "assets/asset_archive.h"
#include "core/archive.h"
#include "scene/entity.h"
#include "scene/transform.h"

namespace Spot {

namespace {

Vector3 GetForward(const Entity& entity) {
    return entity.GetTransform().GetWorldRotation() * Vector3{0.0f, 0.0f, -1.0f};
}

} // namespace

void Light::Serialize(Archive& archive) {
    archive.Field("color", m_color, {.tooltip = "The light's color, before its intensity scales it"});
    archive.Field("intensity", m_intensity,
                  {.min = 0.0f,
                   .max = 100000.0f,
                   .speed = 0.05f,
                   .tooltip = "The illuminance it gives: to every surface facing it for ambient and directional "
                              "lights, and at one unit of distance for point and spot lights, which fall off with "
                              "the square of the distance"});
    archive.Field("castsShadows", m_castingShadows, {.tooltip = "Whether meshes block the light"});
}

LightData Light::GetBaseLightData(LightType type) const {
    LightData light{.type = type, .color = m_color, .intensity = m_intensity};
    light.castsShadows = m_castingShadows && type != LightType::Ambient;
    return light;
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
    archive.Field("range", m_range,
                  {.min = 0.0f, .max = 1000.0f, .speed = 0.05f, .tooltip = "How far the light reaches"});
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
    archive.Field("range", m_range,
                  {.min = 0.0f, .max = 1000.0f, .speed = 0.05f, .tooltip = "How far the light reaches"});
    archive.Field("innerConeAngle", m_innerConeAngle,
                  {.min = 0.0f, .max = ToRadians(89.0f), .angle = true, .tooltip = "Where the light starts to fade"});
    archive.Field("outerConeAngle", m_outerConeAngle,
                  {.min = 0.0f, .max = ToRadians(89.0f), .angle = true, .tooltip = "Where the light ends"});
}

} // namespace Spot
