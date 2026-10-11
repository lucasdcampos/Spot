#include "particle_effects.h"
#include <algorithm>
#include <cmath>

SPOT_COMPONENT(ParticleEffects);
SPOT_COMPONENT(ImpactParticles);

namespace {
ParticleEffect MakeEffect(Spot::Color color, Spot::ParticleRange lifetime, Spot::ParticleRange speed,
                          Spot::ParticleRange size, std::uint32_t count, bool additive) {
    ParticleEffect effect;
    auto& settings = effect.settings;
    settings.maxParticles = 512;
    settings.playOnStart = false;
    settings.emissionRate = 0;
    settings.loop = false;
    settings.shape = Spot::ParticleShape::Cone;
    settings.radius = 0.03f;
    settings.coneAngle = 0.8f;
    settings.lifetime = lifetime;
    settings.speed = speed;
    settings.size = size;
    settings.startColor = color;
    settings.colorOverLifetime =
        Spot::Gradient({{0, Spot::Color::White}, {0.2f, Spot::Color::White}, {1, {1, 1, 1, 0}}});
    settings.sizeOverLifetime = Spot::Curve::Linear(1, additive ? 0 : 2.5f);
    settings.blend = additive ? Spot::ParticleBlend::Additive : Spot::ParticleBlend::Alpha;
    settings.softDistance = additive ? 0 : 0.3f;
    effect.count = count;
    return effect;
}
ParticleEffectPool& Pool(Spot::Scene& scene) {
    if (auto* pool = scene.GetSystem<ParticleEffectPool>())
        return *pool;
    return scene.AddSystem<ParticleEffectPool>();
}
Spot::Quaternion Face(Spot::Vector3 direction) {
    direction = Spot::Normalize(direction);
    if (Spot::LengthSquared(direction) < 0.5f)
        return {};
    const Spot::Vector3 up{0, 1, 0};
    const float dot = std::clamp(Spot::Dot(up, direction), -1.0f, 1.0f);
    if (dot < -0.9999f)
        return Spot::Quaternion::FromAxisAngle({1, 0, 0}, Spot::Pi);
    if (dot > 0.9999f)
        return {};
    return Spot::Quaternion::FromAxisAngle(Spot::Normalize(Spot::Cross(up, direction)), std::acos(dot));
}
} // namespace
ParticleEffects::ParticleEffects()
    : muzzle(MakeEffect({6, 3, 0.6f, 1}, {0.03f, 0.08f}, {0.5f, 2}, {0.08f, 0.18f}, 8, true)),
      sparks(MakeEffect({5, 2.5f, 0.4f, 1}, {0.1f, 0.5f}, {2, 6}, {0.025f, 0.055f}, 18, true)),
      concreteDust(MakeEffect({0.4f, 0.38f, 0.34f, 0.5f}, {0.4f, 1}, {0.3f, 1.5f}, {0.08f, 0.2f}, 12, false)),
      woodDust(MakeEffect({0.4f, 0.2f, 0.07f, 0.7f}, {0.4f, 0.9f}, {0.4f, 2}, {0.04f, 0.1f}, 12, false)),
      fire(MakeEffect({8, 2, 0.2f, 1}, {0.15f, 0.6f}, {2, 6}, {0.2f, 0.8f}, 48, true)),
      smoke(MakeEffect({0.1f, 0.09f, 0.08f, 0.6f}, {1, 3}, {0.3f, 1.5f}, {0.4f, 1}, 32, false)),
      debris(MakeEffect({0.2f, 0.12f, 0.05f, 1}, {0.3f, 1.2f}, {3, 9}, {0.03f, 0.1f}, 24, false)),
      beam(MakeEffect({1.2f, 2.8f, 4, 1}, {0.04f, 0.12f}, {0, 0}, {0.02f, 0.05f}, 1, true)) {
    for (auto* effect : {&muzzle, &concreteDust, &woodDust, &fire, &smoke, &beam})
        effect->settings.gravity = {};
    sparks.settings.facing = Spot::ParticleFacing::Stretched;
    fire.settings.shape = debris.settings.shape = Spot::ParticleShape::Sphere;
    smoke.settings.gravity = {0, 0.5f, 0};
    beam.settings.shape = Spot::ParticleShape::Point;
}
void ParticleEffect::Serialize(Spot::Archive& archive) {
    archive.Field("settings", settings);
    archive.Field("texture", texture);
    archive.Field("count", count, {.min = 0, .max = 512});
    if (archive.IsLoading())
        count = std::min(count, 512u);
}
void ParticleEffects::Serialize(Spot::Archive& archive) {
    archive.Field("muzzle", muzzle);
    archive.Field("sparks", sparks);
    archive.Field("concreteDust", concreteDust);
    archive.Field("woodDust", woodDust);
    archive.Field("fire", fire);
    archive.Field("smoke", smoke);
    archive.Field("debris", debris);
    archive.Field("beam", beam);
    archive.Field("beamEmissionRate", beamEmissionRate, {.min = 0, .max = 2000});
}
ParticleEffects* ParticleEffects::Find(Spot::Scene& scene) {
    ParticleEffects* found = nullptr;
    scene.ForEachComponent<ParticleEffects>([&](ParticleEffects& bank) {
        if (!found && bank.IsActiveAndEnabled())
            found = &bank;
    });
    return found;
}
void ParticleEffects::Muzzle(Spot::Scene& scene, Spot::Vector3 position, Spot::Vector3 direction) {
    if (auto* bank = Find(scene))
        Pool(scene).Spawn(bank->muzzle, position, direction);
}
void ParticleEffects::Impact(Spot::Scene& scene, Spot::Vector3 position, Spot::Vector3 normal, SurfaceKind surface) {
    if (auto* bank = Find(scene)) {
        const auto& effect = surface == SurfaceKind::Metal  ? bank->sparks
                             : surface == SurfaceKind::Wood ? bank->woodDust
                                                            : bank->concreteDust;
        Pool(scene).Spawn(effect, position + normal * 0.02f, normal);
        if (surface == SurfaceKind::Wood)
            Pool(scene).Spawn(bank->debris, position, normal, 0.4f);
    }
}
void ParticleEffects::Explosion(Spot::Scene& scene, Spot::Vector3 position, float radius) {
    if (auto* bank = Find(scene)) {
        const float scale = std::clamp(radius / 5, 0.2f, 4.0f);
        for (const auto* effect : {&bank->fire, &bank->smoke, &bank->debris})
            Pool(scene).Spawn(*effect, position, {0, 1, 0}, scale);
    }
}
void ParticleEffects::Beam(Spot::Scene& scene, Spot::Vector3 from, Spot::Vector3 to, float deltaTime,
                           float& emissionRemainder) {
    auto* bank = Find(scene);
    if (!bank || !std::isfinite(deltaTime) || deltaTime <= 0)
        return;
    emissionRemainder += std::clamp(deltaTime, 0.0f, 0.1f) * std::clamp(bank->beamEmissionRate, 0.0f, 2000.0f);
    const auto count = static_cast<std::uint32_t>(emissionRemainder);
    emissionRemainder -= static_cast<float>(count);
    if (count == 0)
        return;
    // Pool bursts spread births along the full beam; the caller retains fractional emission.
    auto& pool = Pool(scene);
    pool.Spawn(bank->beam, (from + to) * 0.5f, to - from, 1, count, Spot::Distance(from, to));
}
ParticleEffectPool::ParticleEffectPool() : SceneSystem(100) {
    m_emitters.reserve(MaxEmitters);
}
Spot::ParticleSystem* ParticleEffectPool::Spawn(const ParticleEffect& effect, Spot::Vector3 position,
                                                Spot::Vector3 direction, float scale, std::uint32_t count,
                                                float beamLength) {
    if (count == 0)
        count = effect.count;
    if (count == 0)
        return nullptr;
    auto& scene = GetScene();
    Spot::Entity* target = nullptr;
    Slot* selected = nullptr;
    std::erase_if(m_emitters, [&](const Slot& slot) { return !scene.GetEntity(slot.entity); });
    for (auto& slot : m_emitters) {
        auto* candidate = scene.GetEntity(slot.entity);
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
    auto& settings = selected->settings;
    settings = effect.settings;
    if (beamLength > 0) {
        settings.shape = Spot::ParticleShape::Box;
        settings.boxSize = {0.02f, beamLength, 0.02f};
    }
    settings.maxParticles = std::min(settings.maxParticles, 512u);
    settings.playOnStart = false;
    settings.emissionRate = 0;
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
    system->SetSettings(settings);
    system->SetTexture(effect.texture);
    system->Stop(true);
    target->SetActive(true);
    system->Emit(std::min(count, 512u));
    return system;
}
void ParticleEffectPool::OnLateUpdate(float) {
    for (const auto& slot : m_emitters) {
        auto* entity = GetScene().GetEntity(slot.entity);
        auto* system = entity ? entity->GetComponent<Spot::ParticleSystem>() : nullptr;
        if (system && !system->GetSimulation().IsAlive())
            entity->SetActive(false);
    }
}

void ImpactParticles::OnStart() {
    m_quiet = Invoke(0.5f, [] {});
}
void ImpactParticles::OnCollisionEnter(const Spot::Collision& collision) {
    const float speed = std::abs(Spot::Dot(collision.relativeVelocity, collision.normal));
    if (IsInvokePending(m_quiet) || speed < minSpeed)
        return;
    m_quiet = Invoke(0.08f, [] {});
    const auto where = collision.pointCount > 0 ? collision.points[0] : GetEntity().GetTransform().GetWorldPosition();
    ParticleEffects::Impact(GetScene(), where, collision.normal, Surface::Of(&GetEntity()));
}
void ImpactParticles::Serialize(Spot::Archive& archive) {
    archive.Field("minSpeed", minSpeed, {.min = 0, .max = 100});
}
