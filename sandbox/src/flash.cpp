#include "flash.h"

#include <algorithm>
#include <cmath>

SPOT_COMPONENT(Flash);

namespace {

// Flashes light what is around them as brightly as they glow at 40% of their range.
float GetLightIntensity(float glow, float range) {
    return glow * (0.4f * range) * (0.4f * range);
}

} // namespace

Spot::Entity& Flash::Spawn(Spot::Scene& scene, Spot::Vector3 position, Spot::Color color, float range, float intensity,
                           float lifetime, float flicker) {
    Spot::Entity& entity = scene.CreateEntity("Flash");
    entity.GetTransform().SetPosition(position);
    entity.AddComponent<Spot::PointLight>(color, GetLightIntensity(intensity, range), range);
    auto& flash = entity.AddComponent<Flash>();
    flash.color = color;
    flash.intensity = intensity;
    flash.lifetime = lifetime;
    flash.flicker = flicker;
    return entity;
}

void Flash::OnStart() {
    SetStrength(intensity);
}

void Flash::OnUpdate(float deltaTime) {
    m_age += deltaTime;
    if (m_age >= lifetime) {
        GetEntity().Destroy();
        return;
    }
    const float left = 1.0f - m_age / std::max(lifetime, 0.001f);
    // Two waves out of step, so the flicker never repeats within a flash.
    const float wobble = 0.5f + 0.25f * (std::sin(m_age * 47.0f) + std::sin(m_age * 29.0f + 1.3f));
    SetStrength(intensity * left * left * (1.0f - std::clamp(flicker, 0.0f, 1.0f) * wobble));
}

void Flash::SetStrength(float strength) {
    if (auto* light = GetEntity().GetComponent<Spot::PointLight>()) {
        light->SetColor(color);
        light->SetIntensity(GetLightIntensity(std::max(strength, 0.0f), light->GetRange()));
    }
}

void Flash::Serialize(Spot::Archive& archive) {
    archive.Field("lifetime", lifetime, {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "Seconds"});
    archive.Field("intensity", intensity, {.min = 0.0f, .max = 100.0f, .speed = 0.1f});
    archive.Field("color", color);
    archive.Field("flicker", flicker, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
}
