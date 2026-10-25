#include "sounds.h"

#include <algorithm>
#include <span>
#include <string_view>

namespace Generator::Sounds {

namespace {

constexpr float Pi = 3.14159265358979f;

// White noise from -1 to 1, the same for a seed everywhere.
class Noise {
public:
    explicit Noise(std::uint32_t seed) : m_state(seed * 0x9e3779b9u + 0x7f4a7c15u) {}

    float Next() {
        m_state ^= m_state << 13;
        m_state ^= m_state >> 17;
        m_state ^= m_state << 5;
        return static_cast<float>(m_state >> 8) / 8388608.0f - 1.0f;
    }

private:
    std::uint32_t m_state;
};

// A sine of the phase, in turns, from arithmetic alone: a parabola corrected toward the sine, within 0.1%.
float Sine(float phase) {
    float x = phase - static_cast<float>(static_cast<int>(phase));
    if (x < 0.0f) {
        x += 1.0f;
    }
    // From -1 to 1 over the turn, the parabola's shape of a sine.
    const float t = x < 0.5f ? x * 4.0f - 1.0f : 3.0f - x * 4.0f;
    const float parabola = (1.0f - t * t);
    const float half = x < 0.5f ? 1.0f : -1.0f;
    const float y = parabola * half;
    return y * (0.775f + 0.225f * (y < 0.0f ? -y : y));
}

// A first-order low-pass filter, which keeps what is slower than its cutoff.
class LowPass {
public:
    LowPass(float cutoff, std::uint32_t sampleRate) { SetCutoff(cutoff, sampleRate); }

    void SetCutoff(float cutoff, std::uint32_t sampleRate) {
        const float rc = 1.0f / (2.0f * Pi * cutoff);
        const float dt = 1.0f / static_cast<float>(sampleRate);
        m_alpha = dt / (rc + dt);
    }

    float Process(float input) {
        m_value += (input - m_value) * m_alpha;
        return m_value;
    }

private:
    float m_alpha = 1.0f;
    float m_value = 0.0f;
};

// A ringing partial of a struck object.
struct Partial {
    float frequency;
    float amplitude;
    // Seconds for it to fall to about a third.
    float decay;
};

Samples Make(float seconds, std::uint32_t sampleRate = 44100) {
    Samples samples{.sampleRate = sampleRate, .data = {}};
    samples.data.resize(static_cast<std::size_t>(seconds * static_cast<float>(sampleRate)));
    return samples;
}

// The factor a value falls by each sample to fall to about a third in the seconds.
float DecayFactor(float seconds, std::uint32_t sampleRate) {
    return 1.0f - 1.0f / (seconds * static_cast<float>(sampleRate));
}

// Adds struck partials, starting at the sample.
void AddPartials(Samples& samples, std::span<const Partial> partials, std::size_t start = 0, float gain = 1.0f) {
    for (const Partial& partial : partials) {
        const float factor = DecayFactor(partial.decay, samples.sampleRate);
        float envelope = partial.amplitude * gain;
        float phase = 0.0f;
        const float step = partial.frequency / static_cast<float>(samples.sampleRate);
        for (std::size_t i = start; i < samples.data.size(); ++i) {
            samples.data[i] += Sine(phase) * envelope;
            phase += step;
            envelope *= factor;
        }
    }
}

// Adds filtered noise that rises over the attack and then falls.
void AddNoiseBurst(Samples& samples, std::uint32_t seed, float cutoff, float attack, float decay, float gain,
                   std::size_t start = 0) {
    Noise noise(seed);
    LowPass filter(cutoff, samples.sampleRate);
    const auto attackSamples = static_cast<std::size_t>(attack * static_cast<float>(samples.sampleRate)) + 1;
    const float factor = DecayFactor(decay, samples.sampleRate);
    float envelope = gain;
    for (std::size_t i = start; i < samples.data.size(); ++i) {
        const std::size_t t = i - start;
        const float rise = t < attackSamples ? static_cast<float>(t) / static_cast<float>(attackSamples) : 1.0f;
        samples.data[i] += filter.Process(noise.Next()) * envelope * rise;
        if (t >= attackSamples) {
            envelope *= factor;
        }
    }
}

// A low thump whose pitch falls, as the body of a shot or a blast.
void AddThump(Samples& samples, float from, float to, float decay, float gain) {
    const float factor = DecayFactor(decay, samples.sampleRate);
    const float glide = DecayFactor(decay * 0.5f, samples.sampleRate);
    float frequency = from;
    float envelope = gain;
    float phase = 0.0f;
    for (float& sample : samples.data) {
        sample += Sine(phase) * envelope;
        phase += frequency / static_cast<float>(samples.sampleRate);
        frequency = to + (frequency - to) * glide;
        envelope *= factor;
    }
}

// Scales the samples so the loudest is at the peak.
Samples Normalize(Samples samples, float peak) {
    float loudest = 0.0f;
    for (const float sample : samples.data) {
        loudest = std::max(loudest, sample < 0.0f ? -sample : sample);
    }
    if (loudest > 0.0f) {
        for (float& sample : samples.data) {
            sample *= peak / loudest;
        }
    }
    return samples;
}

// Fades the first and last samples, so a sound neither starts nor ends with a click.
Samples FadeEdges(Samples samples, float seconds = 0.002f) {
    const auto count =
        std::min(static_cast<std::size_t>(seconds * static_cast<float>(samples.sampleRate)), samples.data.size() / 2);
    for (std::size_t i = 0; i < count; ++i) {
        const float gain = static_cast<float>(i) / static_cast<float>(count);
        samples.data[i] *= gain;
        samples.data[samples.data.size() - 1 - i] *= gain;
    }
    return samples;
}

// Makes the end flow into the start: the extra tail is mixed over the start as it fades in, then cut off.
Samples Loop(Samples samples, float seconds) {
    const auto overlap = static_cast<std::size_t>(seconds * static_cast<float>(samples.sampleRate));
    const std::size_t length = samples.data.size() - overlap;
    for (std::size_t i = 0; i < overlap; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(overlap);
        samples.data[i] = samples.data[i] * t + samples.data[length + i] * (1.0f - t);
    }
    samples.data.resize(length);
    return samples;
}

// How a footstep sounds on a surface.
struct StepSound {
    // The thud of the heel, which carries the weight, in hertz.
    float thud;
    float thudGain;
    // The scuff of the sole: noise softened above the cutoff, in hertz, falling in the seconds.
    float scuff;
    float scuffDecay;
    // Grains crushed underfoot, as grit on concrete; 0 for none.
    float grit;
    // What the surface rings with once struck, as hollow boards and plates do.
    std::span<const Partial> ring;
    float ringGain;
};

// Noise softened above the cutoff by two filters in a row, so it falls off steeply, as muffled sounds do, and without
// the rumble below 70 hertz, which only muddies a mix.
void AddSoftNoise(Samples& samples, std::uint32_t seed, float cutoff, float attack, float decay, float gain,
                  std::size_t start) {
    Noise noise(seed);
    LowPass first(cutoff, samples.sampleRate);
    LowPass second(cutoff, samples.sampleRate);
    LowPass rumble(70.0f, samples.sampleRate);
    const auto attackSamples = static_cast<std::size_t>(attack * static_cast<float>(samples.sampleRate)) + 1;
    const float factor = DecayFactor(decay, samples.sampleRate);
    float envelope = gain;
    for (std::size_t i = start; i < samples.data.size(); ++i) {
        const std::size_t t = i - start;
        const float rise = t < attackSamples ? static_cast<float>(t) / static_cast<float>(attackSamples) : 1.0f;
        const float soft = second.Process(first.Process(noise.Next()));
        samples.data[i] += (soft - rumble.Process(soft)) * envelope * rise;
        if (t >= attackSamples) {
            envelope *= factor;
        }
    }
}

// A footstep in two parts: the heel lands with a soft thud and a scuff, and the toe follows a few hundredths of a
// second later, quieter. Variants change the timing, the pitch and the noise, so steps never repeat exactly.
Samples Footstep(const StepSound& sound, std::uint32_t seed, int variant) {
    Samples samples = Make(0.32f);
    const auto rate = static_cast<float>(samples.sampleRate);
    const float shift = 1.0f + 0.05f * static_cast<float>(variant % 2 == 0 ? variant : -variant);
    const auto toe = static_cast<std::size_t>((0.05f + 0.012f * static_cast<float>(variant)) * rate);

    // The heel's thud: a short, low sine that sinks in pitch as the weight settles, with a soft attack.
    const float factor = DecayFactor(0.03f, samples.sampleRate);
    const auto attack = static_cast<std::size_t>(0.004f * rate);
    float frequency = sound.thud * shift * 1.4f;
    float envelope = sound.thudGain;
    float phase = 0.0f;
    for (std::size_t i = 0; i < samples.data.size(); ++i) {
        const float rise = i < attack ? static_cast<float>(i) / static_cast<float>(attack) : 1.0f;
        samples.data[i] += Sine(phase) * envelope * rise;
        phase += frequency / rate;
        frequency = sound.thud * shift + (frequency - sound.thud * shift) * 0.9995f;
        envelope *= factor;
    }
    AddSoftNoise(samples, seed, sound.scuff * shift, 0.003f, sound.scuffDecay, 1.0f, 0);
    AddSoftNoise(samples, seed + 1, sound.scuff * shift * 1.15f, 0.006f, sound.scuffDecay * 0.8f, 0.5f, toe);
    AddPartials(samples, sound.ring, 0, sound.ringGain);
    AddPartials(samples, sound.ring, toe, sound.ringGain * 0.4f);
    if (sound.grit > 0.0f) {
        // Sparse grains, each a tiny click, thinning out as the foot settles.
        Noise chance(seed + 2);
        Noise sign(seed + 3);
        LowPass soften(sound.grit, samples.sampleRate);
        const float gritFactor = DecayFactor(0.06f, samples.sampleRate);
        float density = 0.02f;
        for (std::size_t i = 0; i < samples.data.size(); ++i) {
            const float grain = chance.Next() * 0.5f + 0.5f < density ? sign.Next() : 0.0f;
            samples.data[i] += soften.Process(grain) * 0.6f;
            density *= gritFactor;
            if (i == toe) {
                density = 0.012f;
            }
        }
    }
    return FadeEdges(Normalize(std::move(samples), 0.6f), 0.004f);
}

// A bubble's note, rising as it shrinks, as struck water rings.
void AddBloop(Samples& samples, std::size_t start, float from, float to, float seconds, float gain) {
    const auto rate = static_cast<float>(samples.sampleRate);
    const auto length = static_cast<std::size_t>(seconds * rate);
    const auto attack = static_cast<std::size_t>(0.002f * rate) + 1;
    float phase = 0.0f;
    for (std::size_t i = 0; i < length && start + i < samples.data.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(length);
        const float rise = i < attack ? static_cast<float>(i) / static_cast<float>(attack) : 1.0f;
        samples.data[start + i] += Sine(phase) * gain * rise * (1.0f - t) * (1.0f - t);
        phase += (from + (to - from) * t) / rate;
    }
}

// The hiss of spray: noise above the cutoff, rising over the attack and then falling.
void AddSpray(Samples& samples, std::uint32_t seed, float cutoff, float attack, float decay, float gain,
              std::size_t start = 0) {
    Noise noise(seed);
    LowPass low(cutoff, samples.sampleRate);
    LowPass soften(9000.0f, samples.sampleRate);
    const auto attackSamples = static_cast<std::size_t>(attack * static_cast<float>(samples.sampleRate)) + 1;
    const float factor = DecayFactor(decay, samples.sampleRate);
    float envelope = gain;
    for (std::size_t i = start; i < samples.data.size(); ++i) {
        const std::size_t t = i - start;
        const float rise = t < attackSamples ? static_cast<float>(t) / static_cast<float>(attackSamples) : 1.0f;
        const float white = noise.Next();
        samples.data[i] += soften.Process(white - low.Process(white)) * envelope * rise;
        if (t >= attackSamples) {
            envelope *= factor;
        }
    }
}

// Drops falling back onto water, at random times between the seconds, fewer and quieter toward the end.
void AddDroplets(Samples& samples, std::uint32_t seed, int count, float from, float to, float gain) {
    Noise noise(seed);
    const auto rate = static_cast<float>(samples.sampleRate);
    for (int drop = 0; drop < count; ++drop) {
        const float early = (noise.Next() + 1.0f) * 0.5f;
        // Squared, so most fall soon after the splash.
        const float at = from + (to - from) * early * early;
        const float size = (noise.Next() + 1.0f) * 0.5f;
        const float loudness = gain * (1.0f - 0.6f * (at - from) / (to - from)) * (0.4f + 0.6f * size);
        const auto start = static_cast<std::size_t>(at * rate);
        AddBloop(samples, start, 900.0f + 1400.0f * (1.0f - size), 1800.0f + 2200.0f * (1.0f - size),
                 0.015f + 0.03f * size, loudness);
        AddSpray(samples, seed + 1000u + static_cast<std::uint32_t>(drop), 2500.0f, 0.0005f, 0.006f, loudness * 0.8f,
                 start);
    }
}

} // namespace

Samples PistolShot() {
    Samples samples = Make(0.4f);
    AddNoiseBurst(samples, 11, 5000.0f, 0.0005f, 0.05f, 1.0f);
    AddNoiseBurst(samples, 12, 900.0f, 0.001f, 0.12f, 0.8f);
    AddThump(samples, 180.0f, 70.0f, 0.06f, 0.9f);
    return FadeEdges(Normalize(std::move(samples), 0.9f));
}

Samples RifleShot() {
    Samples samples = Make(0.3f);
    AddNoiseBurst(samples, 21, 7000.0f, 0.0003f, 0.03f, 1.0f);
    AddNoiseBurst(samples, 22, 1500.0f, 0.001f, 0.08f, 0.6f);
    AddThump(samples, 220.0f, 90.0f, 0.04f, 0.7f);
    return FadeEdges(Normalize(std::move(samples), 0.85f));
}

Samples ShotgunShot() {
    Samples samples = Make(0.7f);
    AddNoiseBurst(samples, 31, 3500.0f, 0.0008f, 0.09f, 1.0f);
    AddNoiseBurst(samples, 32, 500.0f, 0.002f, 0.25f, 1.0f);
    AddThump(samples, 140.0f, 50.0f, 0.12f, 1.0f);
    return FadeEdges(Normalize(std::move(samples), 0.95f));
}

Samples RocketLaunch() {
    Samples samples = Make(0.9f);
    Noise noise(41);
    LowPass filter(400.0f, samples.sampleRate);
    for (std::size_t i = 0; i < samples.data.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(samples.data.size());
        // The whoosh opens up and closes again as the rocket leaves.
        const float open = t < 0.2f ? t / 0.2f : 1.0f - (t - 0.2f) / 0.8f;
        filter.SetCutoff(300.0f + 2500.0f * open, samples.sampleRate);
        samples.data[i] = filter.Process(noise.Next()) * open;
    }
    AddThump(samples, 120.0f, 60.0f, 0.1f, 0.5f);
    return FadeEdges(Normalize(std::move(samples), 0.8f));
}

Samples Explosion() {
    Samples samples = Make(3.6f);
    const auto rate = static_cast<float>(samples.sampleRate);
    // The crack of the shock front, the blast's roar and its low boom.
    AddNoiseBurst(samples, 51, 7000.0f, 0.0005f, 0.04f, 0.9f);
    AddNoiseBurst(samples, 52, 300.0f, 0.008f, 0.6f, 1.8f);
    AddThump(samples, 85.0f, 28.0f, 0.6f, 1.4f);
    // The rumble rolling back from the hills.
    AddNoiseBurst(samples, 53, 110.0f, 0.25f, 1.4f, 0.9f, static_cast<std::size_t>(0.08f * rate));
    // Debris landing around.
    Noise noise(54);
    for (int piece = 0; piece < 14; ++piece) {
        const float at = 0.5f + 2.0f * (noise.Next() + 1.0f) * 0.5f;
        AddNoiseBurst(samples, 55 + static_cast<std::uint32_t>(piece), 2500.0f, 0.0005f, 0.02f,
                      0.06f + 0.06f * (noise.Next() + 1.0f) * 0.5f, static_cast<std::size_t>(at * rate));
    }
    return FadeEdges(Normalize(std::move(samples), 0.98f));
}

Samples SplashSmall() {
    Samples samples = Make(0.6f);
    const auto rate = static_cast<float>(samples.sampleRate);
    AddSpray(samples, 201, 1500.0f, 0.002f, 0.05f, 0.9f);
    AddNoiseBurst(samples, 202, 900.0f, 0.001f, 0.03f, 0.5f);
    AddBloop(samples, static_cast<std::size_t>(0.01f * rate), 400.0f, 1100.0f, 0.06f, 0.6f);
    AddDroplets(samples, 203, 6, 0.08f, 0.45f, 0.25f);
    return FadeEdges(Normalize(std::move(samples), 0.8f));
}

Samples SplashLarge() {
    Samples samples = Make(2.4f);
    const auto rate = static_cast<float>(samples.sampleRate);
    // The water's mass struck, its whoosh and spray, and a big bubble closing.
    AddThump(samples, 70.0f, 40.0f, 0.15f, 0.8f);
    AddNoiseBurst(samples, 211, 600.0f, 0.005f, 0.25f, 1.2f);
    AddSpray(samples, 212, 1200.0f, 0.01f, 0.35f, 1.0f);
    AddBloop(samples, static_cast<std::size_t>(0.02f * rate), 180.0f, 520.0f, 0.15f, 0.6f);
    // What was thrown up rains back down.
    AddSpray(samples, 214, 2500.0f, 0.4f, 0.7f, 0.35f, static_cast<std::size_t>(0.3f * rate));
    AddDroplets(samples, 213, 70, 0.35f, 2.2f, 0.35f);
    return FadeEdges(Normalize(std::move(samples), 0.9f));
}

Samples Reload() {
    Samples samples = Make(0.6f);
    static constexpr Partial Clack[] = {{1800.0f, 0.5f, 0.02f}, {3100.0f, 0.4f, 0.015f}, {650.0f, 0.4f, 0.03f}};
    AddNoiseBurst(samples, 61, 6000.0f, 0.0005f, 0.01f, 0.6f);
    AddPartials(samples, Clack);
    const auto second = static_cast<std::size_t>(0.35f * static_cast<float>(samples.sampleRate));
    AddNoiseBurst(samples, 62, 6000.0f, 0.0005f, 0.012f, 0.7f, second);
    AddPartials(samples, Clack, second, 1.2f);
    return FadeEdges(Normalize(std::move(samples), 0.7f));
}

Samples ImpactConcrete() {
    Samples samples = Make(0.25f);
    AddNoiseBurst(samples, 71, 1800.0f, 0.001f, 0.04f, 1.0f);
    static constexpr Partial Body[] = {{95.0f, 0.6f, 0.05f}};
    AddPartials(samples, Body);
    return FadeEdges(Normalize(std::move(samples), 0.8f));
}

Samples ImpactWood() {
    Samples samples = Make(0.35f);
    AddNoiseBurst(samples, 81, 3000.0f, 0.0005f, 0.015f, 0.6f);
    static constexpr Partial Knock[] = {{210.0f, 0.8f, 0.07f}, {345.0f, 0.5f, 0.05f}, {590.0f, 0.3f, 0.03f}};
    AddPartials(samples, Knock);
    return FadeEdges(Normalize(std::move(samples), 0.8f));
}

Samples ImpactMetal() {
    Samples samples = Make(1.0f);
    AddNoiseBurst(samples, 91, 6000.0f, 0.0003f, 0.01f, 0.5f);
    static constexpr Partial Ring[] = {
        {523.0f, 0.6f, 0.35f}, {1307.0f, 0.4f, 0.25f}, {2153.0f, 0.3f, 0.15f}, {3387.0f, 0.2f, 0.08f}};
    AddPartials(samples, Ring);
    return FadeEdges(Normalize(std::move(samples), 0.75f));
}

Samples FootstepConcrete(int variant) {
    return Footstep({.thud = 85.0f,
                     .thudGain = 0.7f,
                     .scuff = 650.0f,
                     .scuffDecay = 0.035f,
                     .grit = 2500.0f,
                     .ring = {},
                     .ringGain = 0.0f},
                    101 + static_cast<std::uint32_t>(variant) * 7, variant);
}

Samples FootstepWood(int variant) {
    static constexpr Partial Boards[] = {{150.0f, 0.5f, 0.06f}, {235.0f, 0.3f, 0.045f}, {390.0f, 0.12f, 0.03f}};
    return Footstep({.thud = 110.0f,
                     .thudGain = 0.6f,
                     .scuff = 800.0f,
                     .scuffDecay = 0.03f,
                     .grit = 0.0f,
                     .ring = Boards,
                     .ringGain = 0.6f},
                    211 + static_cast<std::uint32_t>(variant) * 7, variant);
}

Samples FootstepMetal(int variant) {
    static constexpr Partial Plate[] = {{340.0f, 0.3f, 0.14f}, {910.0f, 0.18f, 0.09f}, {1630.0f, 0.1f, 0.05f}};
    return Footstep({.thud = 95.0f,
                     .thudGain = 0.6f,
                     .scuff = 1300.0f,
                     .scuffDecay = 0.025f,
                     .grit = 0.0f,
                     .ring = Plate,
                     .ringGain = 0.5f},
                    321 + static_cast<std::uint32_t>(variant) * 7, variant);
}

Samples PhysicsGunHum() {
    // Whole cycles of every part in the second it lasts, so it loops without a seam.
    Samples samples = Make(1.0f);
    const auto rate = static_cast<float>(samples.sampleRate);
    for (std::size_t i = 0; i < samples.data.size(); ++i) {
        const float t = static_cast<float>(i) / rate;
        const float tremolo = 0.8f + 0.2f * Sine(t * 4.0f);
        samples.data[i] = (Sine(t * 110.0f) * 0.6f + Sine(t * 220.0f) * 0.3f + Sine(t * 331.0f) * 0.1f) * tremolo;
    }
    return Normalize(std::move(samples), 0.5f);
}

Samples Engine() {
    // It fires 100 times a second, so every part makes whole cycles in the second it loops over.
    Samples samples = Make(1.25f, 22050);
    const auto rate = static_cast<float>(samples.sampleRate);
    Noise noise(141);
    Noise beats(142);
    LowPass rumble(900.0f, samples.sampleRate);
    LowPass exhaust(1800.0f, samples.sampleRate);
    int cycle = -1;
    float strength = 1.0f;
    for (std::size_t i = 0; i < samples.data.size(); ++i) {
        const float t = static_cast<float>(i) / rate;
        const float firing = t * 100.0f;
        // Each firing is a little stronger or weaker than the one before, and dies down before the next.
        if (static_cast<int>(firing) != cycle) {
            cycle = static_cast<int>(firing);
            strength = 0.8f + 0.2f * beats.Next();
        }
        const float left = 1.0f - (firing - static_cast<float>(cycle));
        const float pulse = left * left * left * strength;
        const float tone = 0.45f * Sine(t * 100.0f) + 0.3f * Sine(t * 200.0f) + 0.12f * Sine(t * 50.0f);
        const float bang =
            pulse * (0.5f * Sine(t * 300.0f) + 0.25f * Sine(t * 700.0f) + 0.35f * rumble.Process(noise.Next()));
        samples.data[i] = exhaust.Process(tone + bang);
    }
    return Normalize(Loop(std::move(samples), 0.25f), 0.5f);
}

Samples TireSqueal() {
    Samples samples = Make(1.25f, 22050);
    const auto rate = static_cast<float>(samples.sampleRate);
    Noise noise(151);
    LowPass high(2600.0f, samples.sampleRate);
    LowPass low(700.0f, samples.sampleRate);
    for (std::size_t i = 0; i < samples.data.size(); ++i) {
        const float t = static_cast<float>(i) / rate;
        const float hiss = noise.Next();
        // Rubber hissing on the road between the two cutoffs, under a whine that wavers.
        const float band = high.Process(hiss) - low.Process(hiss);
        const float whine = Sine(t * 920.0f + 0.004f * Sine(t * 7.0f)) * (0.6f + 0.4f * Sine(t * 13.0f));
        samples.data[i] = band * 0.6f + whine * 0.35f;
    }
    return Normalize(Loop(std::move(samples), 0.25f), 0.4f);
}

Samples Wind() {
    Samples samples = Make(6.5f, 22050);
    Noise noise(131);
    LowPass filter(500.0f, samples.sampleRate);
    LowPass gust(0.3f, samples.sampleRate);
    Noise gusts(132);
    for (float& sample : samples.data) {
        // Gusts come and go, slowly.
        const float strength = 0.5f + 4.0f * gust.Process(gusts.Next());
        filter.SetCutoff(250.0f + 300.0f * std::clamp(strength, 0.0f, 1.5f), samples.sampleRate);
        sample = filter.Process(noise.Next()) * std::clamp(strength, 0.2f, 1.5f);
    }
    return Normalize(Loop(std::move(samples), 0.5f), 0.5f);
}

Samples Music() {
    // Four chords of two seconds, played softly as pads over a bass, which ring down before the loop starts over.
    Samples samples = Make(8.0f, 22050);
    const auto rate = static_cast<float>(samples.sampleRate);
    static constexpr float Chords[4][4] = {
        {220.0f, 261.63f, 329.63f, 110.0f},
        {174.61f, 220.0f, 261.63f, 87.31f},
        {196.0f, 246.94f, 293.66f, 98.0f},
        {164.81f, 207.65f, 246.94f, 82.41f},
    };
    const std::size_t chordLength = samples.data.size() / 4;
    for (std::size_t chord = 0; chord < 4; ++chord) {
        for (std::size_t i = 0; i < chordLength; ++i) {
            const float t = static_cast<float>(i) / rate;
            const float rise = std::min(t / 0.4f, 1.0f);
            const float fall = 1.0f - t / 2.0f;
            float value = 0.0f;
            for (int note = 0; note < 3; ++note) {
                const float frequency = Chords[chord][note];
                value += (Sine(t * frequency) + 0.3f * Sine(t * frequency * 2.0f)) * 0.25f;
            }
            value += Sine(t * Chords[chord][3]) * 0.4f * fall;
            samples.data[chord * chordLength + i] = value * rise * (0.4f + 0.6f * fall);
        }
    }
    return FadeEdges(Normalize(std::move(samples), 0.6f), 0.01f);
}

Samples Click() {
    Samples samples = Make(0.05f);
    static constexpr Partial Tick[] = {{2200.0f, 1.0f, 0.006f}, {4400.0f, 0.3f, 0.003f}};
    AddPartials(samples, Tick);
    return FadeEdges(Normalize(std::move(samples), 0.6f), 0.0005f);
}

std::vector<std::byte> EncodeWav(const Samples& samples) {
    std::vector<std::byte> wav;
    const auto append = [&wav](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            wav.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xffu));
        }
    };
    const auto appendText = [&wav](std::string_view text) {
        for (const char c : text) {
            wav.push_back(static_cast<std::byte>(c));
        }
    };
    const auto dataSize = static_cast<std::uint32_t>(samples.data.size() * 2);
    appendText("RIFF");
    append(36 + dataSize, 4);
    appendText("WAVEfmt ");
    append(16, 4);
    append(1, 2);
    append(1, 2);
    append(samples.sampleRate, 4);
    append(samples.sampleRate * 2, 4);
    append(2, 2);
    append(16, 2);
    appendText("data");
    append(dataSize, 4);
    for (const float sample : samples.data) {
        const float scaled = std::clamp(sample, -1.0f, 1.0f) * 32767.0f;
        const auto value = static_cast<std::int16_t>(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
        append(static_cast<std::uint16_t>(value), 2);
    }
    return wav;
}

} // namespace Generator::Sounds
