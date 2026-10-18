#include "particle_effects.h"

#include "day_cycle.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

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
Spot::Gradient Fade(std::initializer_list<Spot::GradientKey> keys) {
    return Spot::Gradient(std::vector<Spot::GradientKey>(keys));
}
// From 1 down toward the floor, quickly at first, as drag slows what flies through the air.
Spot::Curve Decay(float rate, float floor) {
    std::vector<Spot::CurveKey> keys;
    for (const float t : {0.0f, 0.1f, 0.25f, 0.5f, 1.0f}) {
        const float fall = (1 - floor) * std::exp(-rate * t);
        keys.push_back({t, floor + fall, -rate * fall, -rate * fall});
    }
    return Spot::Curve(std::move(keys));
}
// From the start toward the end, quickly at first, as a cloud swells and then spreads ever slower.
Spot::Curve Grow(float start, float end, float rate) {
    std::vector<Spot::CurveKey> keys;
    for (const float t : {0.0f, 0.1f, 0.25f, 0.5f, 1.0f}) {
        const float left = (end - start) * std::exp(-rate * t);
        keys.push_back({t, end - left, rate * left, rate * left});
    }
    return Spot::Curve(std::move(keys));
}
constexpr Spot::Color Clear{1, 1, 1, 0};
constexpr Spot::ParticleRange AnyAngle{0, 6.2831853f};
// How much light falls on lit effects: daylight, with a little left at night for the lamps and the moon.
float GetLight(Spot::Scene& scene) {
    const auto* cycle = scene.FindComponent<DayCycle>();
    return cycle ? 0.12f + 0.88f * std::clamp(cycle->GetDaylight(), 0.0f, 1.0f) : 1.0f;
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
    : muzzleFlash(MakeEffect({14, 8, 3, 1}, {0.035f, 0.05f}, {0, 0}, {0.16f, 0.24f}, 2, true)),
      muzzleFlame(MakeEffect({10, 5, 1.6f, 1}, {0.03f, 0.05f}, {4, 10}, {0.05f, 0.09f}, 5, true)),
      muzzleSmoke(MakeEffect({0.62f, 0.62f, 0.6f, 0.32f}, {0.7f, 1.5f}, {0.4f, 1.6f}, {0.05f, 0.1f}, 4, false)),
      sparks(MakeEffect({5, 2.5f, 0.4f, 1}, {0.1f, 0.5f}, {2, 6}, {0.025f, 0.055f}, 18, true)),
      concreteDust(MakeEffect({0.4f, 0.38f, 0.34f, 0.5f}, {0.4f, 1}, {0.3f, 1.5f}, {0.08f, 0.2f}, 12, false)),
      woodDust(MakeEffect({0.4f, 0.2f, 0.07f, 0.7f}, {0.4f, 0.9f}, {0.4f, 2}, {0.04f, 0.1f}, 12, false)),
      blastFlash(MakeEffect({24, 15, 6, 1}, {0.1f, 0.16f}, {0, 0}, {2.5f, 3.5f}, 3, true)),
      fireball(MakeEffect({10, 4.5f, 1.2f, 1}, {0.9f, 1.6f}, {4, 11}, {1.3f, 2.3f}, 40, false)),
      explosionSmoke(MakeEffect({0.2f, 0.19f, 0.18f, 0.9f}, {3.5f, 5}, {1, 3.5f}, {1.1f, 2}, 24, false)),
      embers(MakeEffect({12, 5, 1.2f, 1}, {0.5f, 1.6f}, {8, 22}, {0.025f, 0.05f}, 50, true)),
      explosionDebris(MakeEffect({0.16f, 0.13f, 0.1f, 1}, {1, 2.4f}, {5, 14}, {0.05f, 0.15f}, 32, false)),
      groundDust(MakeEffect({0.45f, 0.39f, 0.31f, 0.75f}, {2, 4}, {4, 10}, {0.7f, 1.4f}, 28, false)),
      splashDroplets(MakeEffect({0.8f, 0.88f, 0.95f, 0.85f}, {0.5f, 1.1f}, {2.5f, 6.5f}, {0.02f, 0.045f}, 28, false)),
      splashSpray(MakeEffect({0.86f, 0.9f, 0.95f, 0.6f}, {0.45f, 0.9f}, {2, 5}, {0.12f, 0.28f}, 10, false)),
      splashMist(MakeEffect({0.9f, 0.93f, 0.96f, 0.28f}, {1.2f, 2.4f}, {0.3f, 1.2f}, {0.3f, 0.6f}, 6, false)),
      ripples(MakeEffect({0.85f, 0.92f, 1, 0.8f}, {1.4f, 2.6f}, {0, 0}, {0.5f, 0.7f}, 2, false)),
      waterPlume(MakeEffect({0.92f, 0.95f, 1, 0.85f}, {1.4f, 2.4f}, {5, 13}, {0.8f, 1.4f}, 72, false)),
      beam(MakeEffect({1.2f, 2.8f, 4, 1}, {0.04f, 0.12f}, {0, 0}, {0.02f, 0.05f}, 1, true)),
      tracer(MakeEffect({7, 4.5f, 2, 1}, {0.05f, 0.05f}, {320, 320}, {0.012f, 0.012f}, 1, true)) {
    for (auto* effect :
         {&muzzleFlash, &muzzleFlame, &muzzleSmoke, &concreteDust, &woodDust, &blastFlash, &beam, &ripples})
        effect->settings.gravity = {};
    for (auto* effect : {&muzzleSmoke, &concreteDust, &woodDust, &explosionSmoke, &explosionDebris, &groundDust,
                         &splashDroplets, &splashSpray, &splashMist, &ripples, &waterPlume})
        effect->lit = true;
    sparks.settings.facing = Spot::ParticleFacing::Stretched;

    // The flash is over in a frame or two; its star turns each time, so no two look the same.
    for (auto* effect : {&muzzleFlash, &blastFlash}) {
        effect->settings.shape = Spot::ParticleShape::Point;
        effect->settings.rotation = AnyAngle;
    }
    muzzleFlash.settings.sizeOverLifetime = Spot::Curve::Linear(0.8f, 1.25f);
    muzzleFlash.settings.colorOverLifetime =
        Fade({{0, Spot::Color::White}, {0.5f, {1, 0.9f, 0.8f, 0.8f}}, {1, {1, 0.7f, 0.5f, 0}}});
    muzzleFlame.settings.coneAngle = 0.22f;
    muzzleFlame.settings.radius = 0.01f;
    muzzleFlame.settings.facing = Spot::ParticleFacing::Stretched;
    muzzleFlame.settings.velocityStretch = 0.02f;
    muzzleFlame.settings.sizeOverLifetime = Spot::Curve::Linear(1, 0.6f);
    // Gun smoke drifts on, slowing, swelling and rising a little as it thins.
    muzzleSmoke.settings.coneAngle = 0.5f;
    muzzleSmoke.settings.gravity = {0, 0.25f, 0};
    muzzleSmoke.settings.speedOverLifetime = Decay(4, 0.05f);
    muzzleSmoke.settings.sizeOverLifetime = Grow(1, 5, 2.5f);
    muzzleSmoke.settings.rotation = AnyAngle;
    muzzleSmoke.settings.rotationOverLifetime = Spot::Curve::Linear(0, 0.6f);
    muzzleSmoke.settings.colorOverLifetime = Fade({{0, Clear}, {0.08f, Spot::Color::White}, {1, Clear}});
    muzzleSmoke.settings.softDistance = 0.2f;

    blastFlash.settings.sizeOverLifetime = Grow(0.6f, 1.3f, 6);
    blastFlash.settings.colorOverLifetime = Fade({{0, Spot::Color::White}, {1, {1, 0.6f, 0.3f, 0}}});
    blastFlash.settings.softDistance = 1;
    // Burning gas: white-hot, cooling through orange and red as it swells and rises, until what is left is soot.
    fireball.settings.shape = Spot::ParticleShape::Sphere;
    fireball.settings.radius = 0.3f;
    fireball.settings.gravity = {0, 2, 0};
    fireball.settings.speedOverLifetime = Decay(6, 0.03f);
    fireball.settings.sizeOverLifetime = Grow(0.5f, 2.2f, 3);
    fireball.settings.rotation = AnyAngle;
    fireball.settings.rotationOverLifetime = Spot::Curve::Linear(0, 0.8f);
    fireball.settings.colorOverLifetime = Fade({{0, {1, 1, 1, 0.95f}},
                                                {0.12f, {0.75f, 0.6f, 0.45f, 1}},
                                                {0.3f, {0.25f, 0.13f, 0.07f, 0.95f}},
                                                {0.5f, {0.012f, 0.02f, 0.06f, 0.9f}},
                                                {1, {0.008f, 0.016f, 0.05f, 0}}});
    fireball.settings.softDistance = 0.8f;
    // The smoke billows up out of the fire, rolling and thinning as it rises for seconds.
    explosionSmoke.settings.shape = Spot::ParticleShape::Sphere;
    explosionSmoke.settings.radius = 0.5f;
    explosionSmoke.settings.gravity = {0, 0.7f, 0};
    explosionSmoke.settings.speedOverLifetime = Decay(3, 0.1f);
    explosionSmoke.settings.sizeOverLifetime = Grow(0.6f, 3, 1.5f);
    explosionSmoke.settings.rotation = {-0.3f, 0.3f};
    explosionSmoke.settings.rotationOverLifetime = Spot::Curve::Linear(0, 0.5f);
    explosionSmoke.settings.colorOverLifetime =
        Fade({{0, Clear}, {0.1f, Spot::Color::White}, {0.5f, {1.1f, 1.1f, 1.1f, 0.75f}}, {1, {1.3f, 1.3f, 1.3f, 0}}});
    explosionSmoke.settings.softDistance = 1.2f;
    // Glowing fragments arc out and fall, cooling from yellow to dull red.
    embers.settings.coneAngle = 1.35f;
    embers.settings.radius = 0.2f;
    embers.settings.facing = Spot::ParticleFacing::Stretched;
    embers.settings.velocityStretch = 0.03f;
    embers.settings.speedOverLifetime = Decay(1.5f, 0.3f);
    embers.settings.sizeOverLifetime = Spot::Curve(1);
    embers.settings.colorOverLifetime =
        Fade({{0, Spot::Color::White}, {0.6f, {1, 0.6f, 0.3f, 1}}, {1, {0.6f, 0.2f, 0.05f, 0}}});
    explosionDebris.settings.coneAngle = 1.1f;
    explosionDebris.settings.rotation = AnyAngle;
    explosionDebris.settings.rotationOverLifetime = Spot::Curve::Linear(0, 12);
    explosionDebris.settings.sizeOverLifetime = Spot::Curve(1);
    explosionDebris.settings.colorOverLifetime =
        Fade({{0, Spot::Color::White}, {0.85f, Spot::Color::White}, {1, Clear}});
    explosionDebris.settings.softDistance = 0;
    // The blast's wind raises a skirt of dust that rolls out along the ground and settles.
    groundDust.settings.coneAngle = 1.45f;
    groundDust.settings.radius = 0.5f;
    groundDust.settings.gravity = {0, -0.4f, 0};
    groundDust.settings.speedOverLifetime = Decay(5, 0.03f);
    groundDust.settings.sizeOverLifetime = Grow(0.6f, 2.8f, 2);
    groundDust.settings.rotation = AnyAngle;
    groundDust.settings.rotationOverLifetime = Spot::Curve::Linear(0, 0.4f);
    groundDust.settings.colorOverLifetime = Fade({{0, Clear}, {0.06f, Spot::Color::White}, {1, {1.1f, 1.1f, 1.1f, 0}}});
    groundDust.settings.softDistance = 1;

    splashDroplets.settings.coneAngle = 0.5f;
    splashDroplets.settings.radius = 0.08f;
    splashDroplets.settings.facing = Spot::ParticleFacing::Stretched;
    splashDroplets.settings.velocityStretch = 0.04f;
    splashDroplets.settings.sizeOverLifetime = Spot::Curve(1);
    splashDroplets.settings.colorOverLifetime = Fade({{0, Spot::Color::White}, {0.8f, Spot::Color::White}, {1, Clear}});
    splashSpray.settings.coneAngle = 0.2f;
    splashSpray.settings.radius = 0.05f;
    splashSpray.settings.gravity = {0, -6, 0};
    splashSpray.settings.speedOverLifetime = Decay(1.2f, 0.4f);
    splashSpray.settings.sizeOverLifetime = Grow(0.6f, 2.2f, 2.5f);
    splashSpray.settings.rotation = AnyAngle;
    splashSpray.settings.colorOverLifetime = Fade({{0, {1, 1, 1, 0.9f}}, {0.6f, {1, 1, 1, 0.6f}}, {1, Clear}});
    splashSpray.settings.softDistance = 0.3f;
    splashMist.settings.shape = Spot::ParticleShape::Sphere;
    splashMist.settings.radius = 0.3f;
    splashMist.settings.gravity = {0, 0.15f, 0};
    splashMist.settings.speedOverLifetime = Decay(2, 0.05f);
    splashMist.settings.sizeOverLifetime = Grow(0.6f, 3, 1.5f);
    splashMist.settings.rotation = AnyAngle;
    splashMist.settings.colorOverLifetime = Fade({{0, Clear}, {0.1f, Spot::Color::White}, {1, Clear}});
    splashMist.settings.softDistance = 0.6f;
    // Rings lying on the water, spreading ever slower as they fade; the shore cuts them off softly.
    ripples.settings.shape = Spot::ParticleShape::Point;
    ripples.settings.facing = Spot::ParticleFacing::Horizontal;
    ripples.settings.sizeOverLifetime = Grow(0.4f, 7, 1.4f);
    ripples.settings.rotation = AnyAngle;
    ripples.settings.colorOverLifetime = Fade({{0, Spot::Color::White}, {0.35f, {1, 1, 1, 0.7f}}, {1, Clear}});
    ripples.settings.softDistance = 0.15f;
    // A blast's column of water climbs, slows, spreads and falls back as it thins.
    waterPlume.settings.coneAngle = 0.28f;
    waterPlume.settings.radius = 0.6f;
    waterPlume.settings.gravity = {0, -7, 0};
    waterPlume.settings.speedOverLifetime = Decay(1, 0.35f);
    waterPlume.settings.sizeOverLifetime = Grow(0.6f, 2.6f, 1.8f);
    waterPlume.settings.rotation = AnyAngle;
    waterPlume.settings.colorOverLifetime = Fade({{0, {1, 1, 1, 0.95f}}, {0.5f, {1, 1, 1, 0.7f}}, {1, Clear}});
    waterPlume.settings.softDistance = 0.8f;

    beam.settings.shape = Spot::ParticleShape::Point;
    tracer.settings.shape = Spot::ParticleShape::Point;
    tracer.settings.gravity = {};
    tracer.settings.facing = Spot::ParticleFacing::Stretched;
    tracer.settings.velocityStretch = 0.006f;
    tracer.settings.sizeOverLifetime = Spot::Curve(1);
    tracer.settings.colorOverLifetime = Spot::Gradient({{0, Spot::Color::White}, {1, Spot::Color::White}});
}
void ParticleEffect::Serialize(Spot::Archive& archive) {
    archive.Field("settings", settings);
    archive.Field("texture", texture);
    archive.Field("count", count, {.min = 0, .max = 512});
    archive.Field("lit", lit, {.tooltip = "Shaded by the daylight rather than glowing"});
    if (archive.IsLoading())
        count = std::min(count, 512u);
}
void ParticleEffects::Serialize(Spot::Archive& archive) {
    archive.Field("muzzleFlash", muzzleFlash);
    archive.Field("muzzleFlame", muzzleFlame);
    archive.Field("muzzleSmoke", muzzleSmoke);
    archive.Field("sparks", sparks);
    archive.Field("concreteDust", concreteDust);
    archive.Field("woodDust", woodDust);
    archive.Field("blastFlash", blastFlash);
    archive.Field("fireball", fireball);
    archive.Field("explosionSmoke", explosionSmoke);
    archive.Field("embers", embers);
    archive.Field("explosionDebris", explosionDebris);
    archive.Field("groundDust", groundDust);
    archive.Field("splashDroplets", splashDroplets);
    archive.Field("splashSpray", splashSpray);
    archive.Field("splashMist", splashMist);
    archive.Field("ripples", ripples);
    archive.Field("waterPlume", waterPlume);
    archive.Field("beam", beam);
    archive.Field("tracer", tracer);
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
void ParticleEffects::Muzzle(Spot::Scene& scene, Spot::Vector3 position, Spot::Vector3 direction, float size) {
    if (auto* bank = Find(scene)) {
        size = std::clamp(size, 0.1f, 4.0f);
        auto& pool = Pool(scene);
        pool.Spawn(bank->muzzleFlash, position, direction, size);
        pool.Spawn(bank->muzzleFlame, position, direction, size);
        pool.Spawn(bank->muzzleSmoke, position, direction, size);
    }
}
void ParticleEffects::Tracer(Spot::Scene& scene, Spot::Vector3 from, Spot::Vector3 to) {
    auto* bank = Find(scene);
    const float distance = Spot::Distance(from, to);
    const float speed = bank ? std::max(bank->tracer.settings.speed.max, 1.0f) : 1.0f;
    // Too close to see it fly.
    if (!bank || distance < 1.5f) {
        return;
    }
    ParticleEffect effect = bank->tracer;
    // It travels from where it starts until it reaches the far end, less its streak, which would show past it.
    const float travel = std::max(distance - speed * effect.settings.velocityStretch * 0.5f, 0.0f);
    effect.settings.lifetime = {travel / speed, travel / speed};
    effect.settings.coneAngle = 0.0f;
    Pool(scene).Spawn(effect, from, to - from);
}
void ParticleEffects::Impact(Spot::Scene& scene, Spot::Vector3 position, Spot::Vector3 normal, SurfaceKind surface) {
    if (auto* bank = Find(scene)) {
        const auto& effect = surface == SurfaceKind::Metal  ? bank->sparks
                             : surface == SurfaceKind::Wood ? bank->woodDust
                                                            : bank->concreteDust;
        Pool(scene).Spawn(effect, position + normal * 0.02f, normal);
        if (surface == SurfaceKind::Wood)
            Pool(scene).Spawn(bank->explosionDebris, position, normal, 0.3f, 8);
    }
}
void ParticleEffects::Explosion(Spot::Scene& scene, Spot::Vector3 position, float radius, Blast blast,
                                std::optional<Spot::Vector3> ground) {
    auto* bank = Find(scene);
    if (!bank)
        return;
    const float scale = std::clamp(radius / 5, 0.2f, 4.0f);
    auto& pool = Pool(scene);
    if (blast == Blast::InAir) {
        for (const auto* effect :
             {&bank->blastFlash, &bank->fireball, &bank->explosionSmoke, &bank->embers, &bank->explosionDebris})
            pool.Spawn(*effect, position, {0, 1, 0}, scale);
    } else if (blast == Blast::OnWater) {
        pool.Spawn(bank->blastFlash, position, {0, 1, 0}, scale);
    }
    if (ground)
        pool.Spawn(bank->groundDust, *ground, {0, 1, 0}, scale);
}
void ParticleEffects::Splash(Spot::Scene& scene, Spot::Vector3 position, float strength) {
    auto* bank = Find(scene);
    if (!bank || !std::isfinite(strength) || strength <= 0)
        return;
    strength = std::min(strength, 10.0f);
    // Harder splashes throw more water, higher and farther.
    const float scale = std::clamp(std::sqrt(strength), 0.45f, 3.0f);
    const float amount = std::clamp(strength, 0.3f, 3.0f);
    const auto share = [&](const ParticleEffect& effect) {
        return std::max(1u, static_cast<std::uint32_t>(static_cast<float>(effect.count) * amount));
    };
    auto& pool = Pool(scene);
    pool.Spawn(bank->splashDroplets, position, {0, 1, 0}, scale, share(bank->splashDroplets));
    pool.Spawn(bank->splashSpray, position, {0, 1, 0}, scale, share(bank->splashSpray));
    if (strength >= 0.5f)
        pool.Spawn(bank->splashMist, position, {0, 1, 0}, scale, share(bank->splashMist));
    if (strength >= 3)
        pool.Spawn(bank->waterPlume, position, {0, 1, 0}, std::clamp(strength / 6, 0.5f, 1.6f));
    // Just above the surface, so the water does not hide them.
    pool.Spawn(bank->ripples, position + Spot::Vector3{0, 0.02f, 0}, {0, 1, 0}, scale,
               bank->ripples.count + (strength >= 1 ? 1u : 0u));
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
    if (effect.lit) {
        const float light = GetLight(scene);
        const auto authored = settings.colorOverLifetime.GetKeys();
        std::vector<Spot::GradientKey> keys(authored.begin(), authored.end());
        if (keys.empty())
            keys.push_back({0, Spot::Color::White});
        for (auto& key : keys)
            key.color = {key.color.r * light, key.color.g * light, key.color.b * light, key.color.a};
        settings.colorOverLifetime.SetKeys(std::move(keys));
    }
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
