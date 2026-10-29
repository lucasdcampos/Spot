#include "gameplay/effects/surface.h"
#include <spot/scene/audio/audio_system.h>

#include "gameplay/effects/effect_library.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

SPOT_COMPONENT(SurfaceLibrary);
SPOT_COMPONENT(Surface);
SPOT_COMPONENT(ImpactSounds);
SPOT_COMPONENT(ImpactParticles);

void SurfaceDefinition::Serialize(Spot::Archive& archive) {
    archive.Field("name", name);
    archive.Field("impact", impact);
    archive.Field("footsteps", footsteps);
    archive.Field("impactEffect", impactEffect, {.tooltip = "The effect a hit plays, from the EffectLibrary"});
}

SurfaceLibrary* SurfaceLibrary::Find(Spot::Scene& scene) {
    SurfaceLibrary* found = nullptr;
    scene.ForEachComponent<SurfaceLibrary>([&](SurfaceLibrary& library) {
        if (!found) {
            found = &library;
        }
    });
    return found;
}

const SurfaceDefinition* SurfaceLibrary::Of(Spot::Scene& scene, const Spot::Entity* entity) {
    const SurfaceLibrary* library = Find(scene);
    const Surface* surface = nullptr;
    for (const Spot::Entity* at = entity; at && !surface; at = at->GetParent()) {
        surface = at->GetComponent<Surface>();
    }
    // A ragdoll's bodies are of what they are the bones of, as a dead NPC's are flesh.
    if (!surface && entity) {
        scene.ForEachComponent<Spot::Ragdoll>([&](Spot::Ragdoll& ragdoll) {
            if (!surface && ragdoll.OwnsBody(entity->GetId())) {
                surface = ragdoll.GetEntity().GetComponent<Surface>();
            }
        });
    }
    return library ? library->Get(static_cast<std::size_t>(std::max(surface ? surface->kind : 0, 0))) : nullptr;
}

const SurfaceDefinition* SurfaceLibrary::Get(std::size_t index) const {
    return index < surfaces.size() ? &surfaces[index] : nullptr;
}

const SurfaceDefinition* SurfaceLibrary::Get(std::string_view name) const {
    const auto found = std::ranges::find(surfaces, name, &SurfaceDefinition::name);
    return found != surfaces.end() ? &*found : nullptr;
}

void SurfaceLibrary::Serialize(Spot::Archive& archive) {
    archive.Field("surfaces", surfaces, {.tooltip = "The first is that of entities without a Surface"});
}

void Surface::Serialize(Spot::Archive& archive) {
    archive.Field("kind", kind,
                  {.min = 0.0f, .max = 255.0f, .tooltip = "The index of its kind in the scene's SurfaceLibrary"});
}

void ImpactSounds::OnStart() {
    m_quiet = Invoke(0.5f, [] {});
}

void ImpactSounds::OnCollisionEnter(const Spot::Collision& collision) {
    const float speed = std::abs(Spot::Dot(collision.relativeVelocity, collision.normal));
    if (IsInvokePending(m_quiet) || speed < minSpeed) {
        return;
    }
    const SurfaceDefinition* surface = SurfaceLibrary::Of(GetScene(), &GetEntity());
    if (!surface || !surface->impact) {
        return;
    }
    m_quiet = Invoke(0.08f, [] {});
    // A little higher or lower each time, so the same prop does not repeat itself exactly.
    m_random = m_random * 1664525u + 1013904223u;
    const float detune = static_cast<float>(m_random >> 8) / 16777216.0f * 0.16f - 0.08f;
    const float volume = std::clamp((speed - minSpeed) / std::max(fullSpeed - minSpeed, 0.01f), 0.1f, 1.0f);
    const Spot::Vector3 where =
        collision.pointCount > 0 ? collision.points[0] : GetEntity().GetTransform().GetWorldPosition();
    GetScene().GetOrAddSystem<Spot::AudioSystem>().PlayOneShot(
        surface->impact.GetShared(), where,
        {.volume = volume,
         .pitch = 1.0f + detune,
         .bus = "Effects",
         .spatialSettings = {.minDistance = 2.0f, .maxDistance = 40.0f},
         .maxInstances = 6});
}

void ImpactSounds::Serialize(Spot::Archive& archive) {
    archive.Field("minSpeed", minSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("fullSpeed", fullSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
}

void ImpactParticles::OnStart() {
    m_quiet = Invoke(0.5f, [] {});
}

void ImpactParticles::OnCollisionEnter(const Spot::Collision& collision) {
    const float speed = std::abs(Spot::Dot(collision.relativeVelocity, collision.normal));
    if (IsInvokePending(m_quiet) || speed < minSpeed) {
        return;
    }
    m_quiet = Invoke(0.08f, [] {});
    const Spot::Vector3 where =
        collision.pointCount > 0 ? collision.points[0] : GetEntity().GetTransform().GetWorldPosition();
    if (const SurfaceDefinition* surface = SurfaceLibrary::Of(GetScene(), &GetEntity())) {
        EffectLibrary::Play(GetScene(), surface->impactEffect, where, collision.normal);
    }
}

void ImpactParticles::Serialize(Spot::Archive& archive) {
    archive.Field("minSpeed", minSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
}

} // namespace Gameplay
