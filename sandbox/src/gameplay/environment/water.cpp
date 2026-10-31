#include "gameplay/environment/water.h"
#include <spot/scene/audio/audio_system.h>
#include <spot/scene/physics/physics_world.h>

#include "gameplay/effects/effect_library.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

SPOT_COMPONENT(Water);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

// How hard something splashes, from how fast it falls in and how heavy it is.
float GetImpact(float speed, float mass) {
    return std::clamp(speed / 5.0f, 0.0f, 2.5f) * std::clamp(std::cbrt(std::max(mass, 0.0f) / 15.0f), 0.4f, 2.5f);
}

bool Has(const std::vector<Spot::EntityId>& ids, Spot::EntityId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

Water* Water::FindAt(Spot::Scene& scene, Spot::Vector3 point) {
    Water* found = nullptr;
    scene.ForEachComponent<Water>([&](Water& water) {
        if (!found && water.Covers(point)) {
            found = &water;
        }
    });
    return found;
}

std::optional<float> Water::Trace(Spot::Vector3 origin, Spot::Vector3 direction, float maxDistance) const {
    const float height = GetHeight();
    if (direction.y >= -1e-6f || origin.y < height) {
        return std::nullopt;
    }
    const float distance = (origin.y - height) / -direction.y;
    if (distance > maxDistance || !Covers(origin + direction * distance)) {
        return std::nullopt;
    }
    return distance;
}

void Water::OnShot(Spot::Vector3 point, float strength) {
    Splash(point, strength);
}

BlastMedium Water::OnBlast(Spot::Vector3 center, float radius) {
    // At the water's surface, or under it, the blast throws the water up; under it, nothing else shows.
    const float surface = GetHeight();
    if (!Covers(center) || center.y >= surface + std::max(radius * 0.35f, 0.3f) || center.y <= surface - depth) {
        return BlastMedium::None;
    }
    const bool underwater = center.y < surface - 0.3f;
    Splash(center, 2.0f + radius * (underwater ? 1.2f : 0.8f));
    return underwater ? BlastMedium::Inside : BlastMedium::Surface;
}

float Water::GetHeight() const {
    return GetEntity().GetTransform().GetWorldPosition().y;
}

Spot::Vector3 Water::GetAxisX() const {
    const Spot::Vector4 axis = GetEntity().GetTransform().GetWorldMatrix() * Spot::Vector4{1.0f, 0.0f, 0.0f, 0.0f};
    return {axis.x, axis.y, axis.z};
}

Spot::Vector3 Water::GetAxisZ() const {
    const Spot::Vector4 axis = GetEntity().GetTransform().GetWorldMatrix() * Spot::Vector4{0.0f, 0.0f, 1.0f, 0.0f};
    return {axis.x, axis.y, axis.z};
}

bool Water::Covers(Spot::Vector3 point) const {
    const Spot::Vector3 offset = point - GetEntity().GetTransform().GetWorldPosition();
    // Within half of each side of the square, along each of its axes.
    for (const Spot::Vector3 axis : {GetAxisX(), GetAxisZ()}) {
        const float length = Spot::LengthSquared(axis);
        if (length <= 0.0f || std::abs(Spot::Dot(offset, axis)) > length * 0.5f) {
            return false;
        }
    }
    return true;
}

bool Water::Contains(Spot::Vector3 point) const {
    const float height = GetHeight();
    return point.y < height && point.y > height - depth && Covers(point);
}

void Water::Splash(Spot::Vector3 point, float strength) {
    if (!std::isfinite(strength) || strength <= 0.0f) {
        return;
    }
    point.y = GetHeight();
    Spot::Scene& scene = GetScene();
    strength = std::min(strength, 10.0f);
    // Harder splashes throw more water, higher and farther.
    const float scale = std::clamp(std::sqrt(strength), 0.45f, 3.0f);
    const float amount = std::clamp(strength, 0.3f, 3.0f);
    EffectLibrary::Play(scene, splashEffect, point, Up, {.scale = scale, .amount = amount});
    if (strength >= 0.5f) {
        EffectLibrary::Play(scene, splashEffect + ".mist", point, Up, {.scale = scale, .amount = amount});
    }
    if (strength >= 3.0f) {
        EffectLibrary::Play(scene, splashEffect + ".plume", point, Up,
                            {.scale = std::clamp(strength / 6.0f, 0.5f, 1.6f)});
    }
    // Just above the surface, so the water does not hide them; another ring for harder splashes.
    EffectLibrary::Play(scene, splashEffect + ".ripples", point + Up * 0.02f, Up,
                        {.scale = scale, .amount = strength >= 1.0f ? 1.5f : 1.0f});
    const bool large = strength >= 1.2f;
    const Spot::AssetRef<Spot::Sound>& sound = large ? largeSplash : smallSplash;
    if (sound) {
        EffectLibrary::PlaySound(
            scene, sound, point,
            {.volume = std::clamp(large ? strength / 3.0f : strength * 2.0f, 0.25f, 1.0f),
             .bus = "Effects",
             .spatialSettings = {.minDistance = 3.0f, .maxDistance = large ? 120.0f : 50.0f},
             .priority = large ? 6 : 2,
             .maxInstances = 6});
    }
}

void Water::Ripple(Spot::Vector3 point, float strength, float scale) {
    if (!IsActiveAndEnabled() || !Covers(point) || !std::isfinite(strength) || strength <= 0.0f ||
        !std::isfinite(scale) || scale <= 0.0f) {
        return;
    }
    const auto* effect = EffectLibrary::Find(GetScene(), splashEffect + ".ripples");
    if (!effect) {
        return;
    }
    point.y = GetHeight() + 0.025f;
    for (ParticleLayer layer : effect->layers) {
        layer.settings.startColor.a *= std::min(strength, 1.0f);
        EffectPool::Get(GetScene()).Spawn(layer, point, Up, std::clamp(scale, 0.1f, 4.0f) * layer.scale, 1);
    }
}

void Water::OnFixedUpdate(float fixedDeltaTime) {
    Spot::Scene& scene = GetScene();
    const Spot::Transform& transform = GetEntity().GetTransform();
    const float height = GetHeight();
    const Spot::Vector3 size{Spot::Length(GetAxisX()), std::max(depth, 0.01f), Spot::Length(GetAxisZ())};
    const Spot::Vector3 center = transform.GetWorldPosition() - Spot::Vector3{0.0f, size.y * 0.5f, 0.0f};
    // Water slows what moves in it, the faster the more, as drag does, but stops nothing.
    const float keep = std::exp(-std::max(drag, 0.0f) * fixedDeltaTime);
    m_insideNow.clear();
    for (const Spot::OverlapHit& hit :
         scene.GetOrAddSystem<Spot::PhysicsWorld>().OverlapBox(center, size, transform.GetWorldRotation())) {
        Spot::Entity* entity = scene.GetEntity(hit.entity);
        auto* body = entity ? entity->GetComponent<Spot::Rigidbody>() : nullptr;
        const Spot::Vector3 position = entity ? entity->GetTransform().GetWorldPosition() : Spot::Vector3{};
        if (!body || body->IsKinematic() || Has(m_insideNow, hit.entity) || !Contains(position)) {
            continue;
        }
        m_insideNow.push_back(hit.entity);
        const Spot::Vector3 velocity = body->GetLinearVelocity();
        if (!Has(m_inside, hit.entity) && -velocity.y >= minSplashSpeed) {
            Splash(position, GetImpact(-velocity.y, body->GetMass()));
        }
        // Bodies at rest on the bottom are left asleep.
        if (Spot::LengthSquared(velocity) > 0.0025f || Spot::LengthSquared(body->GetAngularVelocity()) > 0.0025f) {
            body->SetLinearVelocity(velocity * keep);
            body->SetAngularVelocity(body->GetAngularVelocity() * keep);
        }
    }
    // Characters jumping or falling in, by their feet.
    scene.ForEachComponent<Spot::CharacterController>([&](Spot::CharacterController& character) {
        Spot::Entity& entity = character.GetEntity();
        const Spot::Vector3 feet = entity.GetTransform().GetWorldPosition() + character.GetCenter() -
                                   Spot::Vector3{0.0f, character.GetHeight() * 0.5f, 0.0f};
        if (!Contains(feet)) {
            return;
        }
        m_insideNow.push_back(entity.GetId());
        const float speed = -character.GetVelocity().y;
        if (!Has(m_inside, entity.GetId()) && speed >= minSplashSpeed) {
            Splash({feet.x, height, feet.z}, GetImpact(speed, character.GetMass()));
        }
    });
    std::swap(m_inside, m_insideNow);
}

void Water::Serialize(Spot::Archive& archive) {
    archive.Field("depth", depth, {.min = 0.0f, .max = 1000.0f, .speed = 0.05f, .tooltip = "Meters below the surface"});
    archive.Field("shotDepth", shotDepth,
                  {.min = 0.0f, .max = 100.0f, .speed = 0.05f, .tooltip = "Meters shots go on under the surface"});
    archive.Field("drag", drag,
                  {.min = 0.0f, .max = 100.0f, .speed = 0.05f, .tooltip = "Share of their speed lost each second"});
    archive.Field("minSplashSpeed", minSplashSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("splashEffect", splashEffect, {.tooltip = "The effects its splashes play are named after it"});
    archive.Field("smallSplash", smallSplash, {.tooltip = "Shots and small things falling in"});
    archive.Field("largeSplash", largeSplash, {.tooltip = "Big things and blasts"});
}

} // namespace Gameplay
