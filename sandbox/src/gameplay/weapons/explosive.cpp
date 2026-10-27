#include "gameplay/weapons/explosive.h"
#include <spot/scene/physics/physics_world.h>

#include "gameplay/camera/camera_shake.h"
#include "gameplay/core/trace_volume.h"
#include "gameplay/effects/effect_library.h"
#include "gameplay/health/damage.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(Explosive);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

} // namespace

void BlastSettings::Serialize(Spot::Archive& archive) {
    archive.Field("radius", radius, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("impulse", impulse, {.min = 0.0f, .max = 100000.0f, .speed = 1.0f, .tooltip = "At the center"});
    archive.Field("damage", damage, {.min = 0.0f, .max = 10000.0f, .speed = 1.0f, .tooltip = "At the center"});
    archive.Field("damageType", damageType);
    archive.Field("effect", effect,
                  {.tooltip = "Its effect, and those named after it with .dust, .water and .underwater"});
    archive.Field("shake", shake, {.min = 0.0f, .max = 1.0f, .speed = 0.005f, .tooltip = "Radians, for 5 meters"});
}

void Explosive::Explode(Spot::Scene& scene, Spot::Vector3 center, const BlastSettings& blast, Spot::EntityId ignore,
                        Spot::EntityId instigator) {
    const BlastMedium medium = Traces::Blast(scene, center, blast.radius);
    const float scale = blast.radius / 5.0f;
    if (medium == BlastMedium::None) {
        EffectLibrary::Play(scene, blast.effect, center, Up, {.scale = scale});
        const float above = 0.3f;
        if (const auto ground = scene.GetOrAddSystem<Spot::PhysicsWorld>().Raycast(
                center + Up * above, -Up, blast.radius * 0.6f + above, {.ignore = ignore})) {
            EffectLibrary::Play(scene, blast.effect + ".dust", ground->point, Up, {.scale = scale});
        }
    } else {
        EffectLibrary::Play(scene, blast.effect + (medium == BlastMedium::Inside ? ".underwater" : ".water"), center,
                            Up, {.scale = scale});
    }
    Spot::Input::RumbleGamepad(0.8f, 0.6f, 0.3f);
    Damage::ApplyRadial(scene, center,
                        {.radius = blast.radius,
                         .amount = blast.damage,
                         .impulse = blast.impulse,
                         .type = blast.damageType,
                         .instigator = instigator,
                         .source = ignore,
                         .ignore = ignore});
    // The blast wave shakes views well beyond where it hurts.
    CameraShake::Add(scene, center, blast.shake * std::clamp(scale, 0.3f, 2.0f), std::max(blast.radius, 1.0f) * 8.0f);
}

void Explosive::Explode(Spot::Vector3 center, Spot::EntityId instigator) {
    if (m_exploded) {
        return;
    }
    m_exploded = true;
    Spot::Scene& scene = GetScene();
    const Spot::EntityId source = GetEntity().GetId();
    const BlastSettings settings = blast;
    GetEntity().Destroy();
    Explode(scene, center, settings, source, instigator);
}

void Explosive::Explode() {
    Explode(GetEntity().GetTransform().GetWorldPosition());
}

void Explosive::Serialize(Spot::Archive& archive) {
    blast.Serialize(archive);
}

} // namespace Gameplay
