#include "sound_bank.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

SPOT_COMPONENT(Surface);
SPOT_COMPONENT(SoundBank);
SPOT_COMPONENT(ImpactSounds);

SurfaceKind Surface::Of(const Spot::Entity* entity) {
    const Surface* surface = entity ? entity->GetComponent<Surface>() : nullptr;
    return surface ? surface->kind : SurfaceKind::Concrete;
}

void Surface::Serialize(Spot::Archive& archive) {
    static constexpr std::array<std::string_view, 3> Kinds = {"Concrete", "Wood", "Metal"};
    archive.Field("kind", kind, {.enumNames = Kinds});
}

void SurfaceSounds::Serialize(Spot::Archive& archive) {
    archive.Field("impact", impact);
    archive.Field("footsteps", footsteps);
}

SoundBank* SoundBank::Find(Spot::Scene& scene) {
    SoundBank* found = nullptr;
    scene.ForEachComponent<SoundBank>([&found](SoundBank& bank) {
        if (!found) {
            found = &bank;
        }
    });
    return found;
}

const SurfaceSounds* SoundBank::GetSurface(SurfaceKind kind) const {
    const auto index = static_cast<std::size_t>(kind);
    return index < surfaces.size() ? &surfaces[index] : nullptr;
}

void SoundBank::PlayClick(Spot::Scene& scene) {
    const SoundBank* bank = Find(scene);
    Spot::AudioDevice* device = Spot::Audio::GetDevice();
    if (bank && device && bank->click) {
        device->Play(bank->click.GetShared(), {.volume = 0.5f, .bus = Spot::Audio::FindBus("Interface")});
    }
}

void SoundBank::Serialize(Spot::Archive& archive) {
    archive.Field("surfaces", surfaces, {.tooltip = "By surface: concrete, wood and metal"});
    archive.Field("explosion", explosion);
    archive.Field("smallSplash", smallSplash, {.tooltip = "Shots and small things falling in water"});
    archive.Field("largeSplash", largeSplash, {.tooltip = "Big things and blasts in water"});
    archive.Field("click", click);
}

void ImpactSounds::OnStart() {
    m_quiet = Invoke(0.5f, [] {});
}

void ImpactSounds::OnCollisionEnter(const Spot::Collision& collision) {
    const float speed = std::abs(Spot::Dot(collision.relativeVelocity, collision.normal));
    if (IsInvokePending(m_quiet) || speed < minSpeed) {
        return;
    }
    const SoundBank* bank = SoundBank::Find(GetScene());
    const SurfaceSounds* sounds = bank ? bank->GetSurface(Surface::Of(&GetEntity())) : nullptr;
    if (!sounds || !sounds->impact) {
        return;
    }
    m_quiet = Invoke(0.08f, [] {});
    // A little higher or lower each time, so the same prop does not repeat itself exactly.
    m_random = m_random * 1664525u + 1013904223u;
    const float detune = static_cast<float>(m_random >> 8) / 16777216.0f * 0.16f - 0.08f;
    const float volume = std::clamp((speed - minSpeed) / std::max(fullSpeed - minSpeed, 0.01f), 0.1f, 1.0f);
    const Spot::Vector3 where =
        collision.pointCount > 0 ? collision.points[0] : GetEntity().GetTransform().GetWorldPosition();
    GetScene().GetAudio().PlayOneShot(sounds->impact.GetShared(), where,
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
