#include "flash.h"

#include <algorithm>

SPOT_COMPONENT(Flash);

namespace {

// Flashes light what is around them as brightly as they glow at 40% of their range.
float GetLightIntensity(float glow, float range) {
    return glow * (0.4f * range) * (0.4f * range);
}

} // namespace

Spot::Entity& Flash::Spawn(Spot::Scene& scene, Spot::Vector3 position, Spot::Color color, float size, float range,
                           float intensity, float lifetime) {
    Spot::Entity& entity = scene.CreateEntity("Flash");
    entity.GetTransform().SetPosition(position);
    entity.GetTransform().SetScale({size, size, size});
    entity.AddComponent<Spot::PointLight>(color, GetLightIntensity(intensity, range), range);
    auto& ball =
        entity.AddComponent<Spot::MeshRenderer>(Spot::Assets::Load<Spot::Mesh>(Spot::BuiltInAssets::SphereMesh),
                                                Spot::Material{.baseColor = Spot::Color::Black});
    ball.SetCastingShadows(false);
    auto& flash = entity.AddComponent<Flash>();
    flash.color = color;
    flash.intensity = intensity;
    flash.lifetime = lifetime;
    return entity;
}

void Flash::OnStart() {
    SetStrength(intensity);
    Spot::TweenSystem::Get(GetScene())
        .Play(
            *this,
            Spot::Tween::Value(std::max(0.0f, lifetime), intensity, 0.0f, [this](float value) { SetStrength(value); }),
            [this] { GetEntity().Destroy(); });
}

void Flash::SetStrength(float strength) {
    if (auto* light = GetEntity().GetComponent<Spot::PointLight>()) {
        light->SetIntensity(GetLightIntensity(strength, light->GetRange()));
    }
    if (auto* ball = GetEntity().GetComponent<Spot::MeshRenderer>()) {
        ball->SetMaterial(
            Spot::Material{.baseColor = Spot::Color::Black,
                           .emissive = {color.r * strength, color.g * strength, color.b * strength, 1.0f}});
    }
}

void Flash::Serialize(Spot::Archive& archive) {
    archive.Field("lifetime", lifetime, {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "Seconds"});
    archive.Field("intensity", intensity, {.min = 0.0f, .max = 100.0f, .speed = 0.1f});
    archive.Field("color", color);
}
