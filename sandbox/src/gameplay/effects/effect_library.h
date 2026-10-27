#pragma once

#include "gameplay/effects/effect_pool.h"

#include <spot.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Gameplay {

// The light an effect gives off, as a Flash.
struct EffectLight {
    // Black for none.
    Spot::Color color = {0.0f, 0.0f, 0.0f, 1.0f};
    // Meters its light reaches, times the scale the effect is played at.
    float range = 5.0f;
    float intensity = 10.0f;
    float lifetime = 0.1f;
    float flicker = 0.0f;
    // Meters along the effect's direction it shines from.
    float offset = 0.0f;

    void Serialize(Spot::Archive& archive);
};

// The sound an effect makes where it plays.
struct EffectSound {
    Spot::AssetRef<Spot::Sound> sound;
    float volume = 1.0f;
    std::string bus = "Effects";
    // Meters within which it is heard at full volume, and beyond which it is not heard.
    float minDistance = 2.0f;
    float maxDistance = 50.0f;
    // Higher keeps it playing when voices run out.
    int priority = 0;
    int maxInstances = 4;

    void Serialize(Spot::Archive& archive);
};

// A named effect: bursts of particles, a light and a sound, played together.
struct EffectDefinition {
    // Such as "explosion" or "impact.wood".
    std::string name;
    std::vector<ParticleLayer> layers;
    EffectLight light;
    EffectSound sound;

    void Serialize(Spot::Archive& archive);
};

struct EffectOptions {
    // Of the size of the particles, clamped to [0.1, 4], and of the reach of the light.
    float scale = 1.0f;
    // Of the count of particles, each layer bursting at least one.
    float amount = 1.0f;
    // Of the sound's volume.
    float volume = 1.0f;
};

// The effects of a scene by name, so what plays one names it rather than holding its particles: a weapon plays its
// "muzzle" and the surface hit its "impact.wood". The library is data, on an entity of the scene; a new effect is a
// new entry, and a game that takes the effects brings its own. Effects the library does not have play nothing.
class EffectLibrary : public Spot::Component {
public:
    std::vector<EffectDefinition> effects;

    // The scene's first enabled library; null when it has none.
    static EffectLibrary* Find(Spot::Scene& scene);
    // The effect of the scene's library; null when there is none of that name.
    static const EffectDefinition* Find(Spot::Scene& scene, std::string_view name);

    // Plays the effect at the position, its particles and light facing the direction.
    static void Play(Spot::Scene& scene, std::string_view name, Spot::Vector3 position,
                     Spot::Vector3 direction = {0.0f, 1.0f, 0.0f}, const EffectOptions& options = {});
    // Plays that many of the effect's particles spread along the segment, as a beam.
    static void PlayAlong(Spot::Scene& scene, std::string_view name, Spot::Vector3 from, Spot::Vector3 to,
                          std::uint32_t count);
    // Sends the effect's particles flying from one point to the other at their speed, gone as they get there, as a
    // tracer; nothing when they are too close to see it fly.
    static void PlayTo(Spot::Scene& scene, std::string_view name, Spot::Vector3 from, Spot::Vector3 to);

    const EffectDefinition* Get(std::string_view name) const;

protected:
    void Serialize(Spot::Archive& archive) override;
};

} // namespace Gameplay
