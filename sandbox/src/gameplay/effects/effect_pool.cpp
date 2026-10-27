#include "gameplay/effects/effect_pool.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

namespace {

Spot::Quaternion Face(Spot::Vector3 direction) {
    direction = Spot::Normalize(direction);
    if (Spot::LengthSquared(direction) < 0.5f) {
        return {};
    }
    const Spot::Vector3 up{0.0f, 1.0f, 0.0f};
    const float dot = std::clamp(Spot::Dot(up, direction), -1.0f, 1.0f);
    if (dot < -0.9999f) {
        return Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, Spot::Pi);
    }
    if (dot > 0.9999f) {
        return {};
    }
    return Spot::Quaternion::FromAxisAngle(Spot::Normalize(Spot::Cross(up, direction)), std::acos(dot));
}

} // namespace

void ParticleLayer::Serialize(Spot::Archive& archive) {
    archive.Field("settings", settings);
    archive.Field("texture", texture);
    archive.Field("count", count, {.min = 0, .max = 512});
    archive.Field("lit", lit, {.tooltip = "Shaded by the scene's light rather than glowing"});
    archive.Field("scale", scale, {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "Of the effect's size"});
    archive.Field("offset", offset,
                  {.min = -10.0f, .max = 10.0f, .speed = 0.005f, .tooltip = "Meters along the effect's direction"});
    if (archive.IsLoading()) {
        count = std::min(count, 512u);
    }
}

EffectPool::EffectPool() : SceneSystem(100) {
    m_emitters.reserve(MaxEmitters);
}

EffectPool& EffectPool::Get(Spot::Scene& scene) {
    if (auto* pool = scene.GetSystem<EffectPool>()) {
        return *pool;
    }
    return scene.AddSystem<EffectPool>();
}

Spot::ParticleSystem* EffectPool::Spawn(const ParticleLayer& layer, Spot::Vector3 position, Spot::Vector3 direction,
                                        float scale, std::uint32_t count, float beamLength) {
    if (count == 0) {
        count = layer.count;
    }
    if (count == 0) {
        return nullptr;
    }
    Spot::Scene& scene = GetScene();
    Spot::Entity* target = nullptr;
    Slot* selected = nullptr;
    std::erase_if(m_emitters, [&](const Slot& slot) { return !scene.GetEntity(slot.entity); });
    for (Slot& slot : m_emitters) {
        Spot::Entity* candidate = scene.GetEntity(slot.entity);
        auto* system = candidate->GetComponent<Spot::ParticleSystem>();
        if (system && !system->GetSimulation().IsAlive()) {
            target = candidate;
            selected = &slot;
            break;
        }
    }
    if (!target) {
        if (m_emitters.size() >= MaxEmitters) {
            ++m_dropped;
            return nullptr;
        }
        target = &scene.CreateEntity("Particle Effect");
        target->AddTag("TransientEffect");
        target->AddComponent<Spot::ParticleSystem>();
        m_emitters.push_back({target->GetId(), {}});
        selected = &m_emitters.back();
    }
    auto* system = target->GetComponent<Spot::ParticleSystem>();
    Spot::ParticleSettings& settings = selected->settings;
    settings = layer.settings;
    if (beamLength > 0.0f) {
        settings.shape = Spot::ParticleShape::Box;
        settings.boxSize = {0.02f, beamLength, 0.02f};
    }
    settings.maxParticles = std::min(settings.maxParticles, 512u);
    settings.playOnStart = false;
    settings.emissionRate = 0.0f;
    settings.bursts.clear();
    settings.loop = false;
    settings.space = Spot::ParticleSpace::World;
    settings.lifetime.min = std::min(settings.lifetime.min, 5.0f);
    settings.lifetime.max = std::min(settings.lifetime.max, 5.0f);
    settings.seed += static_cast<std::uint32_t>(++m_sequence);
    target->GetTransform().SetPosition(position);
    target->GetTransform().SetRotation(Face(direction));
    target->GetTransform().SetScale({scale, scale, scale});
    settings.size.min *= scale;
    settings.size.max *= scale;
    if (layer.lit) {
        const auto authored = settings.colorOverLifetime.GetKeys();
        std::vector<Spot::GradientKey> keys(authored.begin(), authored.end());
        if (keys.empty()) {
            keys.push_back({0.0f, Spot::Color::White});
        }
        for (Spot::GradientKey& key : keys) {
            key.color = {key.color.r * m_light, key.color.g * m_light, key.color.b * m_light, key.color.a};
        }
        settings.colorOverLifetime.SetKeys(std::move(keys));
    }
    system->SetSettings(settings);
    system->SetTexture(layer.texture);
    system->Stop(true);
    target->SetActive(true);
    system->Emit(std::min(count, 512u));
    return system;
}

void EffectPool::OnLateUpdate(float /*deltaTime*/) {
    for (const Slot& slot : m_emitters) {
        Spot::Entity* entity = GetScene().GetEntity(slot.entity);
        auto* system = entity ? entity->GetComponent<Spot::ParticleSystem>() : nullptr;
        if (system && !system->GetSimulation().IsAlive()) {
            entity->SetActive(false);
        }
    }
}

} // namespace Gameplay
