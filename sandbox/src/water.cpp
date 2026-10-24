#include "water.h"
#include "spot/scene/audio/audio_system.h"
#include "spot/scene/physics/physics_world.h"

#include "particle_effects.h"
#include "player_movement.h"
#include "sound_bank.h"

#include <algorithm>
#include <cmath>

SPOT_COMPONENT(Water);

namespace {

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
        if (!found && water.IsActiveAndEnabled() && water.Covers(point)) {
            found = &water;
        }
    });
    return found;
}

std::optional<WaterHit> Water::Raycast(Spot::Scene& scene, Spot::Vector3 origin, Spot::Vector3 direction,
                                       float maxDistance) {
    std::optional<WaterHit> nearest;
    if (direction.y >= -1e-6f) {
        return nearest;
    }
    scene.ForEachComponent<Water>([&](Water& water) {
        const float height = water.GetHeight();
        if (!water.IsActiveAndEnabled() || origin.y < height) {
            return;
        }
        const float distance = (origin.y - height) / -direction.y;
        if (distance > maxDistance || (nearest && distance >= nearest->distance)) {
            return;
        }
        const Spot::Vector3 point = origin + direction * distance;
        if (water.Covers(point)) {
            nearest = WaterHit{&water, point, distance};
        }
    });
    return nearest;
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
    ParticleEffects::Splash(scene, point, strength);
    const SoundBank* bank = SoundBank::Find(scene);
    const bool large = strength >= 1.2f;
    const Spot::AssetRef<Spot::Sound>* sound = bank ? (large ? &bank->largeSplash : &bank->smallSplash) : nullptr;
    if (sound && *sound) {
        scene.GetOrAddSystem<Spot::AudioSystem>().PlayOneShot(
            sound->GetShared(), point,
            {.volume = std::clamp(large ? strength / 3.0f : strength * 2.0f, 0.25f, 1.0f),
             .bus = "Effects",
             .spatialSettings = {.minDistance = 3.0f, .maxDistance = large ? 120.0f : 50.0f},
             .priority = large ? 6 : 2,
             .maxInstances = 6});
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
    // Players jumping or falling in.
    scene.ForEachComponent<PlayerMovement>([&](PlayerMovement& movement) {
        Spot::Entity& player = movement.GetEntity();
        const auto* body = player.GetComponent<Spot::CharacterController>();
        if (!body) {
            return;
        }
        const Spot::Vector3 feet = player.GetTransform().GetWorldPosition() + body->GetCenter() -
                                   Spot::Vector3{0.0f, body->GetHeight() * 0.5f, 0.0f};
        if (!Contains(feet)) {
            return;
        }
        m_insideNow.push_back(player.GetId());
        const float speed = -body->GetVelocity().y;
        if (!Has(m_inside, player.GetId()) && speed >= minSplashSpeed) {
            Splash({feet.x, height, feet.z}, GetImpact(speed, body->GetMass()));
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
}
