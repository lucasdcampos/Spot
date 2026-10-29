#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// The sandbox's sounds, synthesized from code with fixed seeds. Like the textures, only arithmetic makes them, so
// every machine writes the same samples.
namespace Generator::Sounds {

// Mono samples from -1 to 1 at a rate.
struct Samples {
    std::uint32_t sampleRate = 44100;
    std::vector<float> data;
};

Samples PistolShot();
Samples RifleShot();
Samples ShotgunShot();
Samples RocketLaunch();
Samples Explosion();
// Water struck: by a shot or something small, and by something big or a blast, which throws water high to rain back.
Samples SplashSmall();
Samples SplashLarge();
Samples Reload();
Samples ImpactConcrete();
Samples ImpactWood();
Samples ImpactMetal();
// Four of each, told apart by the variant, so steps do not all sound the same.
Samples FootstepConcrete(int variant);
Samples FootstepWood(int variant);
Samples FootstepMetal(int variant);
// Loops: each ends where it starts.
Samples PhysicsGunHum();
// A four-cylinder engine at 3000 revolutions a minute, which the car pitches to its own, and its tires squealing.
Samples Engine();
// Four blades passing 28 times a second at 420 rotor revolutions a minute.
Samples HelicopterRotor();
Samples TireSqueal();
// A car's horn: two tones a major third apart, as most cars sound them, looping while held.
Samples Horn();
Samples Wind();
Samples Music();
Samples Click();

// A 16-bit PCM WAV file of the samples.
std::vector<std::byte> EncodeWav(const Samples& samples);

} // namespace Generator::Sounds
