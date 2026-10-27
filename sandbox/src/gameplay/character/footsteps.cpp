#include "gameplay/character/footsteps.h"
#include <spot/scene/audio/audio_system.h>
#include <spot/scene/physics/physics_world.h>

#include "gameplay/character/character_movement.h"
#include "gameplay/effects/surface.h"

namespace Gameplay {

SPOT_COMPONENT(Footsteps);

void Footsteps::OnStart() {
    if (auto* movement = GetEntity().GetComponent<CharacterMovement>()) {
        m_step = movement->OnFootstep.Connect([this](float loudness) { Play(loudness); });
    }
}

float Footsteps::Next() {
    m_random ^= m_random << 13;
    m_random ^= m_random >> 17;
    m_random ^= m_random << 5;
    return static_cast<float>(m_random >> 8) / 16777216.0f;
}

void Footsteps::Play(float loudness) {
    const auto* controller = GetEntity().GetComponent<Spot::CharacterController>();
    if (!controller || !IsActiveAndEnabled()) {
        return;
    }
    const Spot::Vector3 feet = GetEntity().GetTransform().GetWorldPosition() + controller->GetCenter() -
                               Spot::Vector3{0.0f, controller->GetHeight() * 0.5f, 0.0f};
    const auto ground = GetScene().GetOrAddSystem<Spot::PhysicsWorld>().Raycast(
        feet + Spot::Vector3{0.0f, 0.2f, 0.0f}, {0.0f, -1.0f, 0.0f}, 0.6f, {.ignore = GetEntity().GetId()});
    const SurfaceDefinition* sounds =
        SurfaceLibrary::Of(GetScene(), ground ? GetScene().GetEntity(ground->entity) : nullptr);
    if (!sounds || sounds->footsteps.empty()) {
        return;
    }
    const std::size_t count = sounds->footsteps.size();
    if (count > 1) {
        m_footstep += 1 + static_cast<std::size_t>(Next() * static_cast<float>(count - 1));
    }
    const Spot::AssetRef<Spot::Sound>& sound = sounds->footsteps[m_footstep % count];
    GetScene().GetOrAddSystem<Spot::AudioSystem>().PlayOneShot(
        sound.GetShared(), feet,
        {.volume = volume * loudness * (0.85f + Next() * 0.15f),
         .pitch = 0.93f + Next() * 0.14f,
         .bus = "Effects",
         .spatialSettings = {.minDistance = 2.0f, .maxDistance = 25.0f},
         .maxInstances = 3});
}

void Footsteps::Serialize(Spot::Archive& archive) {
    archive.Field("volume", volume, {.min = 0.0f, .max = 4.0f, .speed = 0.01f});
}

} // namespace Gameplay
