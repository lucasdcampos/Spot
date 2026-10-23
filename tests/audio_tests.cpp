#include "log_capture.h"

#include <spot/audio/mixer.h>

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

void AppendBytes(std::vector<std::byte>& out, std::string_view text) {
    for (const char c : text) {
        out.push_back(static_cast<std::byte>(c));
    }
}

void AppendLittleEndian(std::vector<std::byte>& out, std::uint32_t value, int byteCount) {
    for (int i = 0; i < byteCount; ++i) {
        out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xff));
    }
}

// A 16-bit PCM WAV file holding the given interleaved samples.
std::vector<std::byte> MakeWav(std::uint32_t sampleRate, std::uint32_t channels,
                               std::span<const std::int16_t> samples) {
    const auto dataSize = static_cast<std::uint32_t>(samples.size() * 2);
    std::vector<std::byte> wav;
    AppendBytes(wav, "RIFF");
    AppendLittleEndian(wav, 36 + dataSize, 4);
    AppendBytes(wav, "WAVEfmt ");
    AppendLittleEndian(wav, 16, 4);
    AppendLittleEndian(wav, 1, 2);
    AppendLittleEndian(wav, channels, 2);
    AppendLittleEndian(wav, sampleRate, 4);
    AppendLittleEndian(wav, sampleRate * channels * 2, 4);
    AppendLittleEndian(wav, channels * 2, 2);
    AppendLittleEndian(wav, 16, 2);
    AppendBytes(wav, "data");
    AppendLittleEndian(wav, dataSize, 4);
    for (const std::int16_t sample : samples) {
        AppendLittleEndian(wav, static_cast<std::uint16_t>(sample), 2);
    }
    return wav;
}

std::vector<std::byte> MakeWav(std::uint32_t sampleRate, std::uint32_t channels,
                               std::initializer_list<std::int16_t> samples) {
    return MakeWav(sampleRate, channels, std::span<const std::int16_t>(samples.begin(), samples.size()));
}

// Mono samples rising a step each frame, so where a stream is in it shows in what it plays.
std::vector<std::int16_t> MakeRamp(std::size_t frames) {
    std::vector<std::int16_t> samples(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        samples[i] = static_cast<std::int16_t>(i % 30000);
    }
    return samples;
}

} // namespace

TEST_CASE("WAV sounds decode to float samples", "[audio]") {
    const auto wav = MakeWav(22050, 2, {0, 16384, -16384, -32768});
    const auto sound = Spot::Sound::LoadFromMemory(wav);

    REQUIRE(sound);
    CHECK(sound->GetSampleRate() == 22050);
    CHECK(sound->GetChannelCount() == 2);
    CHECK(sound->GetFrameCount() == 2);
    REQUIRE(sound->GetSamples().size() == 4);
    CHECK(sound->GetSamples()[0] == Catch::Approx(0.0f));
    CHECK(sound->GetSamples()[1] == Catch::Approx(0.5f));
    CHECK(sound->GetSamples()[2] == Catch::Approx(-0.5f));
    CHECK(sound->GetSamples()[3] == Catch::Approx(-1.0f));
}

TEST_CASE("Ogg Vorbis sounds decode to float samples", "[audio]") {
    // A quarter second at 44100 Hz: a 440 Hz sine at half volume on the left, silence on the right.
    const auto sound = Spot::Sound::Load(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "sine.ogg");

    REQUIRE(sound);
    CHECK(sound->GetSampleRate() == 44100);
    CHECK(sound->GetChannelCount() == 2);
    CHECK(sound->GetFrameCount() == 11025);
    CHECK(sound->GetDuration() == Catch::Approx(0.25f));

    float leftPeak = 0.0f;
    float rightPeak = 0.0f;
    const auto samples = sound->GetSamples();
    for (std::size_t i = 0; i < samples.size(); i += 2) {
        leftPeak = std::max(leftPeak, std::abs(samples[i]));
        rightPeak = std::max(rightPeak, std::abs(samples[i + 1]));
    }
    // Vorbis is lossy, so the levels are only close to the original ones.
    CHECK(leftPeak == Catch::Approx(0.5f).margin(0.05f));
    CHECK(rightPeak < 0.01f);
}

TEST_CASE("Sounds that cannot be read or decoded are reported", "[audio]") {
    const auto capture = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({capture});

    CHECK_FALSE(Spot::Sound::Load(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "missing.wav"));
    const std::array<std::byte, 4> garbage{};
    CHECK_FALSE(Spot::Sound::LoadFromMemory(garbage));
    const std::array<std::byte, 8> brokenOgg{std::byte{'O'}, std::byte{'g'}, std::byte{'g'}, std::byte{'S'}};
    CHECK_FALSE(Spot::Sound::LoadFromMemory(brokenOgg));
    CHECK(capture->GetRecords().size() == 3);
}

TEST_CASE("WAV sounds with more than two channels are rejected", "[audio]") {
    const auto capture = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({capture});

    CHECK_FALSE(Spot::Sound::LoadFromMemory(MakeWav(48000, 3, {0, 0, 0})));
    CHECK(capture->GetRecords().size() == 1);
}

TEST_CASE("Sounds can be built from samples", "[audio]") {
    const Spot::Sound sound(48000, 1, {0.25f, -0.25f, 0.5f});

    CHECK(sound.GetFrameCount() == 3);
    CHECK(sound.GetDuration() == Catch::Approx(3.0f / 48000.0f));
}

namespace {

std::shared_ptr<const Spot::Sound> MakeSound(std::uint32_t sampleRate, std::uint32_t channels,
                                             std::vector<float> samples) {
    return std::make_shared<Spot::Sound>(sampleRate, channels, std::move(samples));
}

std::vector<float> Mix(Spot::Detail::Mixer& mixer, std::size_t frameCount) {
    std::vector<float> output(frameCount * 2, 123.0f);
    mixer.Mix(output);
    return output;
}

// Mixes long enough for fades and ramps to end.
void Settle(Spot::Detail::Mixer& mixer) {
    Mix(mixer, 1000);
}

} // namespace

TEST_CASE("The mixer plays mono sounds on both channels until they end", "[audio][mixer]") {
    Spot::Detail::Mixer mixer(48000);
    const auto instance = mixer.Play(MakeSound(48000, 1, {0.1f, 0.2f, 0.3f}));

    CHECK(mixer.IsPlaying(instance));
    CHECK(Mix(mixer, 4) == std::vector<float>{0.1f, 0.1f, 0.2f, 0.2f, 0.3f, 0.3f, 0.0f, 0.0f});
    CHECK_FALSE(mixer.IsPlaying(instance));
    CHECK(Mix(mixer, 1) == std::vector<float>{0.0f, 0.0f});
}

TEST_CASE("The mixer keeps the channels of stereo sounds apart", "[audio][mixer]") {
    Spot::Detail::Mixer mixer(48000);
    mixer.Play(MakeSound(48000, 2, {0.1f, -0.1f, 0.2f, -0.2f}));

    CHECK(Mix(mixer, 2) == std::vector<float>{0.1f, -0.1f, 0.2f, -0.2f});
}

TEST_CASE("The mixer adds sounds together and clips the result", "[audio][mixer]") {
    Spot::Detail::Mixer mixer(48000);
    const auto sound = MakeSound(48000, 1, {0.25f, 0.75f});
    mixer.Play(sound);
    mixer.Play(sound);

    CHECK(Mix(mixer, 2) == std::vector<float>{0.5f, 0.5f, 1.0f, 1.0f});
}

TEST_CASE("The mixer applies volume, master volume and pan, ramping their changes", "[audio][mixer]") {
    Spot::Detail::Mixer mixer(48000);
    const auto sound = MakeSound(48000, 1, {0.5f, 0.5f, 0.5f, 0.5f});
    const auto instance = mixer.Play(sound, {.volume = 0.5f, .pan = -1.0f, .loop = true});

    CHECK(Mix(mixer, 1) == std::vector<float>{0.25f, 0.0f});

    // Changes take a few milliseconds instead of clicking.
    mixer.SetPan(instance, 1.0f);
    const auto ramp = Mix(mixer, 1);
    CHECK(ramp[0] > 0.2f);
    CHECK(ramp[1] < 0.05f);
    Settle(mixer);
    CHECK(Mix(mixer, 1) == std::vector<float>{0.0f, 0.25f});

    mixer.SetPan(instance, 0.5f);
    mixer.SetVolume(instance, 1.0f);
    mixer.SetMasterVolume(0.5f);
    CHECK(mixer.GetMasterVolume() == 0.5f);
    Settle(mixer);
    const auto output = Mix(mixer, 1);
    CHECK(output[0] == Catch::Approx(0.125f));
    CHECK(output[1] == Catch::Approx(0.25f));
}

TEST_CASE("Looping sounds start over until stopped", "[audio][mixer]") {
    Spot::Detail::Mixer mixer(48000);
    const auto instance = mixer.Play(MakeSound(48000, 1, {0.1f, 0.2f}), {.loop = true});

    CHECK(Mix(mixer, 3) == std::vector<float>{0.1f, 0.1f, 0.2f, 0.2f, 0.1f, 0.1f});
    CHECK(Mix(mixer, 1) == std::vector<float>{0.2f, 0.2f});
    CHECK(mixer.IsPlaying(instance));

    mixer.Stop(instance);
    CHECK_FALSE(mixer.IsPlaying(instance));
    // It fades out over a few milliseconds instead of clicking.
    const auto fading = Mix(mixer, 1);
    CHECK(fading[0] > 0.0f);
    CHECK(fading[0] < 0.1f);
    Settle(mixer);
    CHECK(Mix(mixer, 1) == std::vector<float>{0.0f, 0.0f});
}

TEST_CASE("The mixer converts sample rates and applies pitch", "[audio][mixer]") {
    Spot::Detail::Mixer mixer(48000);

    SECTION("A sound at half the rate is stretched with interpolated frames") {
        mixer.Play(MakeSound(24000, 1, {0.0f, 1.0f, 0.0f}));
        const auto output = Mix(mixer, 4);
        CHECK(output[0] == Catch::Approx(0.0f));
        CHECK(output[2] == Catch::Approx(0.5f));
        CHECK(output[4] == Catch::Approx(1.0f));
        CHECK(output[6] == Catch::Approx(0.5f));
    }

    SECTION("Twice the pitch skips every other frame") {
        const auto instance = mixer.Play(MakeSound(48000, 1, {0.1f, 0.2f, 0.3f, 0.4f}), {.pitch = 2.0f});
        CHECK(Mix(mixer, 2) == std::vector<float>{0.1f, 0.1f, 0.3f, 0.3f});
        CHECK_FALSE(mixer.IsPlaying(instance));
    }

    SECTION("Pitch can change while playing") {
        const auto instance = mixer.Play(MakeSound(48000, 1, {0.1f, 0.2f, 0.3f, 0.4f}));
        CHECK(Mix(mixer, 1) == std::vector<float>{0.1f, 0.1f});
        mixer.SetPitch(instance, 2.0f);
        CHECK(Mix(mixer, 2) == std::vector<float>{0.2f, 0.2f, 0.4f, 0.4f});
    }
}

TEST_CASE("Paused sounds fade out, stay silent and resume where they stopped", "[audio][mixer]") {
    Spot::Detail::Mixer mixer(48000);
    // Shorter than the time it stays paused, so it would end if it went on.
    const auto instance = mixer.Play(MakeSound(48000, 1, std::vector<float>(2000, 0.5f)));

    CHECK(Mix(mixer, 1) == std::vector<float>{0.5f, 0.5f});
    mixer.SetPaused(instance, true);
    CHECK(mixer.IsPaused(instance));
    CHECK(mixer.IsPlaying(instance));
    CHECK(Mix(mixer, 1)[0] > 0.4f);
    Settle(mixer);
    for (int i = 0; i < 3; ++i) {
        Settle(mixer);
    }
    CHECK(Mix(mixer, 1) == std::vector<float>{0.0f, 0.0f});
    CHECK(mixer.IsPlaying(instance));

    mixer.SetPaused(instance, false);
    CHECK_FALSE(mixer.IsPaused(instance));
    const auto rising = Mix(mixer, 1);
    CHECK(rising[0] > 0.0f);
    CHECK(rising[0] < 0.1f);
    Settle(mixer);
    CHECK(Mix(mixer, 1) == std::vector<float>{0.5f, 0.5f});
}

TEST_CASE("Instances of ended sounds, the default instance and null sounds match nothing", "[audio][mixer]") {
    Spot::Detail::Mixer mixer(48000);
    const auto sound = MakeSound(48000, 1, {0.5f});

    CHECK(mixer.Play(nullptr) == Spot::SoundInstance{});
    CHECK_FALSE(mixer.IsPlaying({}));

    const auto first = mixer.Play(sound);
    Mix(mixer, 1);
    const auto second = mixer.Play(sound);
    CHECK(first != second);
    CHECK_FALSE(mixer.IsPlaying(first));
    CHECK(mixer.IsPlaying(second));

    // Calls on an ended instance must not reach the playback that followed it.
    mixer.Stop(first);
    mixer.SetVolume(first, 0.0f);
    CHECK(Mix(mixer, 1) == std::vector<float>{0.5f, 0.5f});

    mixer.Play(sound, {.loop = true});
    mixer.StopAll();
    Settle(mixer);
    CHECK(Mix(mixer, 1) == std::vector<float>{0.0f, 0.0f});
}

TEST_CASE("The mixer releases sounds that ended on the next play", "[audio][mixer]") {
    Spot::Detail::Mixer mixer(48000);
    const auto sound = MakeSound(48000, 1, {0.5f});
    mixer.Play(sound);
    Mix(mixer, 1);

    CHECK(sound.use_count() == 2);
    mixer.Play(MakeSound(48000, 1, {0.5f}));
    CHECK(sound.use_count() == 1);
}

TEST_CASE("Buses scale and mute their sounds, and pass them to their parents", "[audio][mixer][bus]") {
    using Spot::AudioBusId;
    Spot::Detail::Mixer mixer(48000);
    const auto music = mixer.AddBus("Music");
    REQUIRE(music.has_value());
    const auto ambience = mixer.AddBus("Ambience", *music);
    REQUIRE(ambience.has_value());
    CHECK(mixer.FindBus("Ambience") == ambience);
    CHECK(mixer.FindBus("Master") == AudioBusId::Master);
    CHECK_FALSE(mixer.FindBus("Voices").has_value());
    CHECK(mixer.GetBusParent(*ambience) == music);
    CHECK_FALSE(mixer.GetBusParent(AudioBusId::Master).has_value());
    CHECK(mixer.GetBuses() == std::vector<AudioBusId>{AudioBusId::Master, *music, *ambience});

    const auto sound = MakeSound(48000, 1, {0.5f, 0.5f, 0.5f, 0.5f});
    mixer.Play(sound, {.loop = true, .bus = *ambience});
    mixer.Play(sound, {.loop = true});
    CHECK(Mix(mixer, 1) == std::vector<float>{1.0f, 1.0f});

    // Volumes multiply on the way up, once their changes have ramped.
    mixer.SetBusVolume(*music, 0.5f);
    mixer.SetBusVolume(*ambience, 0.5f);
    Settle(mixer);
    CHECK(Mix(mixer, 1) == std::vector<float>{0.625f, 0.625f});
    mixer.SetMasterVolume(0.5f);
    Settle(mixer);
    CHECK(Mix(mixer, 1) == std::vector<float>{0.3125f, 0.3125f});
    mixer.SetMasterVolume(1.0f);

    mixer.SetBusMuted(*music, true);
    CHECK(mixer.IsBusMuted(*music));
    Settle(mixer);
    CHECK(Mix(mixer, 1) == std::vector<float>{0.5f, 0.5f});
    mixer.SetBusMuted(*music, false);

    // Removing a bus hands its sounds and its buses to its parent.
    mixer.SetBusVolume(*ambience, 1.0f);
    Settle(mixer);
    CHECK(mixer.RemoveBus(*music));
    CHECK_FALSE(mixer.HasBus(*music));
    CHECK(mixer.GetBusParent(*ambience) == AudioBusId::Master);
    CHECK(Mix(mixer, 1) == std::vector<float>{1.0f, 1.0f});
    CHECK_FALSE(mixer.RemoveBus(AudioBusId::Master));

    // Sounds for buses that do not exist play on the master one.
    mixer.StopAll();
    Settle(mixer);
    mixer.Play(sound, {.bus = static_cast<AudioBusId>(31)});
    CHECK(Mix(mixer, 1) == std::vector<float>{0.5f, 0.5f});
}

TEST_CASE("Buses never form loops, and there are at most 32", "[audio][mixer][bus]") {
    using Spot::AudioBusId;
    Spot::Detail::Mixer mixer(48000);
    const AudioBusId effects = *mixer.AddBus("Effects");
    const AudioBusId weapons = *mixer.AddBus("Weapons", effects);
    CHECK_FALSE(mixer.SetBusParent(effects, weapons));
    CHECK_FALSE(mixer.SetBusParent(effects, effects));
    CHECK_FALSE(mixer.SetBusParent(AudioBusId::Master, effects));
    CHECK(mixer.SetBusParent(weapons, AudioBusId::Master));
    CHECK(mixer.SetBusParent(effects, weapons));
    CHECK(mixer.GetBusParent(effects) == weapons);

    for (std::size_t i = 3; i < Spot::Detail::Mixer::MaxBuses; ++i) {
        CHECK(mixer.AddBus("Bus").has_value());
    }
    CHECK_FALSE(mixer.AddBus("One Too Many").has_value());
    // A removed bus's id goes to the next one.
    CHECK(mixer.RemoveBus(weapons));
    CHECK(mixer.AddBus("Again") == weapons);
}

TEST_CASE("Each bus's peak meter tells the loudest sample that left it", "[audio][mixer][bus]") {
    using Spot::AudioBusId;
    Spot::Detail::Mixer mixer(48000);
    const AudioBusId effects = *mixer.AddBus("Effects");
    mixer.SetBusVolume(effects, 0.5f);
    // Once the bus's volume has ramped down.
    Settle(mixer);
    mixer.Play(MakeSound(48000, 2, {0.2f, -0.8f, 0.4f, 0.1f}), {.bus = effects});
    mixer.Play(MakeSound(48000, 1, {0.1f}));
    Mix(mixer, 600);
    const Spot::AudioPeak peak = mixer.GetBusPeak(effects);
    CHECK(peak.left == Catch::Approx(0.2f));
    CHECK(peak.right == Catch::Approx(0.4f));
    CHECK(mixer.GetBusPeak(AudioBusId::Master).left == Catch::Approx(0.2f));
    // The mono sound adds 0.1 to the -0.4 of the first frame.
    CHECK(mixer.GetBusPeak(AudioBusId::Master).right == Catch::Approx(0.3f));

    Mix(mixer, 10);
    CHECK(mixer.GetBusPeak(effects).right == 0.0f);
    CHECK(mixer.GetBusPeak(static_cast<AudioBusId>(1000)).left == 0.0f);
}

TEST_CASE("Soloed buses are heard alone, with the buses below them", "[audio][mixer][bus]") {
    using Spot::AudioBusId;
    Spot::Detail::Mixer mixer(48000);
    const AudioBusId effects = *mixer.AddBus("Effects");
    const AudioBusId weapons = *mixer.AddBus("Weapons", effects);
    const AudioBusId music = *mixer.AddBus("Music");
    const auto sound = MakeSound(48000, 1, {0.1f});
    mixer.Play(sound, {.loop = true, .bus = weapons});
    mixer.Play(sound, {.loop = true, .bus = effects});
    mixer.Play(sound, {.loop = true, .bus = music});
    mixer.Play(sound, {.loop = true});
    CHECK(Mix(mixer, 1)[0] == Catch::Approx(0.4f));

    // Effects and the weapons below it; neither the music nor what plays on the master bus itself.
    mixer.SetBusSolo(effects, true);
    CHECK(mixer.IsBusSolo(effects));
    Settle(mixer);
    CHECK(Mix(mixer, 1)[0] == Catch::Approx(0.2f));

    mixer.SetBusSolo(music, true);
    Settle(mixer);
    CHECK(Mix(mixer, 1)[0] == Catch::Approx(0.3f));

    mixer.SetBusSolo(effects, false);
    mixer.SetBusSolo(music, false);
    Settle(mixer);
    CHECK(Mix(mixer, 1)[0] == Catch::Approx(0.4f));
}

TEST_CASE("The audio settings give the device their buses, in any order", "[audio][bus][settings]") {
    Spot::SettingsSections sections;
    sections.audio.masterVolume = 0.8f;
    sections.audio.buses = {{.name = "Weapons", .parent = "Effects", .volume = 0.5f},
                            {.name = "Effects", .muted = true},
                            {.name = "Music", .parent = "Missing"}};
    Spot::SettingsSections loaded;
    REQUIRE(loaded.LoadFromString(sections.SaveToString()));
    REQUIRE(loaded.audio.buses.size() == 3);
    CHECK(loaded.audio.masterVolume == 0.8f);
    CHECK(loaded.audio.buses[0].parent == "Effects");
    CHECK(loaded.audio.buses[0].volume == 0.5f);
    CHECK(loaded.audio.buses[1].muted);

    Spot::AudioDevice device({.enabled = false});
    const Spot::AudioBusId kept = *device.AddBus("Voices");
    loaded.audio.Apply(device);
    CHECK(device.GetMasterVolume() == 0.8f);
    const auto weapons = device.FindBus("Weapons");
    const auto effects = device.FindBus("Effects");
    REQUIRE(weapons.has_value());
    REQUIRE(effects.has_value());
    CHECK(device.GetBusParent(*weapons) == effects);
    CHECK(device.GetBusVolume(*weapons) == 0.5f);
    CHECK(device.IsBusMuted(*effects));
    CHECK(device.GetBusParent(*device.FindBus("Music")) == Spot::AudioBusId::Master);
    CHECK(device.HasBus(kept));

    // Applied again, nothing is added twice, and a loop is refused.
    loaded.audio.buses[1].parent = "Weapons";
    loaded.audio.Apply(device);
    CHECK(device.GetBuses().size() == 5);
    CHECK(device.GetBusParent(*effects) == Spot::AudioBusId::Master);

    CHECK(Spot::AudioSettings::MakeDefault().buses.size() == 4);
    CHECK(Spot::SettingsSections{}.audio.buses.empty());
}

TEST_CASE("New sounds take the voices of lower priorities, the oldest first, past the limit",
          "[audio][mixer][voices]") {
    Spot::Detail::Mixer mixer(48000);
    mixer.SetMaxVoices(2);
    const auto sound = MakeSound(48000, 1, {0.1f});
    const auto music = mixer.Play(sound, {.loop = true, .priority = 10});
    const auto first = mixer.Play(sound, {.loop = true});
    CHECK(mixer.GetVoiceCount() == 2);

    // The oldest of the lowest priority gives its voice.
    const auto second = mixer.Play(sound, {.loop = true});
    CHECK(mixer.IsPlaying(music));
    CHECK_FALSE(mixer.IsPlaying(first));
    CHECK(mixer.IsPlaying(second));
    CHECK(mixer.GetVoiceCount() == 2);

    // A sound ranked below every one playing does not play.
    CHECK(mixer.Play(sound, {.priority = -1}) == Spot::SoundInstance{});
    CHECK(mixer.IsPlaying(second));

    // One ranked above takes the lowest.
    const auto alarm = mixer.Play(sound, {.loop = true, .priority = 20});
    CHECK(mixer.IsPlaying(alarm));
    CHECK(mixer.IsPlaying(music));
    CHECK_FALSE(mixer.IsPlaying(second));
}

TEST_CASE("A sound plays at most its instance limit at once", "[audio][mixer][voices]") {
    Spot::Detail::Mixer mixer(48000);
    const auto impact = MakeSound(48000, 1, {0.1f});
    std::vector<Spot::SoundInstance> instances;
    for (int i = 0; i < 4; ++i) {
        instances.push_back(mixer.Play(impact, {.loop = true, .maxInstances = 3}));
    }
    const auto other = mixer.Play(MakeSound(48000, 1, {0.1f}), {.loop = true, .maxInstances = 1});

    CHECK_FALSE(mixer.IsPlaying(instances[0]));
    CHECK(mixer.IsPlaying(instances[1]));
    CHECK(mixer.IsPlaying(instances[3]));
    CHECK(mixer.IsPlaying(other));
    CHECK(mixer.GetVoiceCount() == 4);
}

TEST_CASE("Sounds can fade in, and fade out as long as asked when stopped", "[audio][mixer][voices]") {
    Spot::Detail::Mixer mixer(48000);
    const auto instance = mixer.Play(MakeSound(48000, 1, {1.0f}), {.loop = true, .fadeIn = 0.01f});
    // Half way through its 480 frames of fade.
    CHECK(Mix(mixer, 240)[478] == Catch::Approx(0.5f).margin(0.01f));
    Settle(mixer);
    CHECK(Mix(mixer, 1)[0] == 1.0f);

    mixer.Stop(instance, 0.1f);
    CHECK_FALSE(mixer.IsPlaying(instance));
    CHECK(mixer.GetVoiceCount() == 0);
    // A tenth of a second is 4800 frames.
    CHECK(Mix(mixer, 2400)[4798] == Catch::Approx(0.5f).margin(0.01f));
    Mix(mixer, 2400);
    CHECK(Mix(mixer, 1)[0] == 0.0f);
}

TEST_CASE("Streamed sounds keep their data encoded", "[audio][stream]") {
    const auto wav = Spot::Sound::CreateStream(MakeWav(22050, 2, {0, 16384, -16384, -32768}));
    REQUIRE(wav);
    CHECK(wav->IsStreamed());
    CHECK(wav->GetSamples().empty());
    CHECK(wav->GetSampleRate() == 22050);
    CHECK(wav->GetChannelCount() == 2);
    CHECK(wav->GetFrameCount() == 2);
    CHECK_FALSE(wav->GetEncodedData().empty());

    const auto ogg = Spot::Sound::LoadStream(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "sine.ogg");
    REQUIRE(ogg);
    CHECK(ogg->GetSampleRate() == 44100);
    CHECK(ogg->GetFrameCount() == 11025);
    CHECK_FALSE(Spot::Sound(48000, 1, {0.5f}).IsStreamed());

    const auto capture = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({capture});
    CHECK_FALSE(Spot::Sound::CreateStream(std::vector<std::byte>(16)));
    CHECK_FALSE(capture->GetRecords().empty());
}

TEST_CASE("Streamed sounds play as they would decoded, looping included", "[audio][stream]") {
    Spot::Detail::Mixer mixer(48000, false);
    const auto ramp = MakeRamp(1000);
    const auto wav = MakeWav(32000, 1, ramp);
    const auto decoded = Spot::Sound::LoadFromMemory(wav);
    const auto streamed = Spot::Sound::CreateStream(wav);
    REQUIRE(decoded);
    REQUIRE(streamed);

    // On opposite sides, so each channel shows one of them; the rates differ, so frames are interpolated.
    const auto a = mixer.Play(decoded, {.pan = -1.0f, .loop = true});
    const auto b = mixer.Play(streamed, {.pan = 1.0f, .loop = true});
    for (int block = 0; block < 4; ++block) {
        const auto output = Mix(mixer, 1000);
        for (std::size_t i = 0; i < output.size(); i += 2) {
            CAPTURE(block, i);
            REQUIRE(output[i + 1] == Catch::Approx(output[i]).margin(1e-6f));
        }
    }
    CHECK(mixer.IsPlaying(a));
    CHECK(mixer.IsPlaying(b));

    // Without looping, it ends with its last frame.
    mixer.StopAll();
    Settle(mixer);
    const auto once = mixer.Play(streamed, {.pitch = 1.5f});
    Mix(mixer, 990);
    CHECK(mixer.IsPlaying(once));
    Mix(mixer, 20);
    CHECK_FALSE(mixer.IsPlaying(once));
}

TEST_CASE("A stream decodes ahead, and waits where it is when it runs dry", "[audio][stream]") {
    Spot::Detail::Mixer mixer(48000, false);
    const auto ramp = MakeRamp(48000 * 2);
    const auto streamed = Spot::Sound::CreateStream(MakeWav(48000, 1, ramp));
    REQUIRE(streamed);
    const auto instance = mixer.Play(streamed);

    // Half a second is decoded before it plays; past it, nothing new comes until the streams update.
    const auto first = Mix(mixer, 24000);
    CHECK(first[2 * 23990] == Catch::Approx(23990.0f / 32768.0f).margin(1e-4f));
    const auto dry = Mix(mixer, 100);
    CHECK(mixer.IsPlaying(instance));

    mixer.UpdateStreams();
    const auto resumed = Mix(mixer, 100);
    // It goes on from where the buffer ran out, losing nothing.
    CHECK(resumed[0] == Catch::Approx(dry[0]).margin(2.0f / 32768.0f));
    CHECK(resumed[2 * 50] > resumed[0]);

    // Updated as it plays, it reaches its end.
    for (int i = 0; i < 10; ++i) {
        mixer.UpdateStreams();
        Mix(mixer, 12000);
    }
    CHECK_FALSE(mixer.IsPlaying(instance));
}

TEST_CASE("The mixer's own thread keeps streams decoded", "[audio][stream]") {
    Spot::Detail::Mixer mixer(48000);
    const auto streamed = Spot::Sound::CreateStream(MakeWav(48000, 1, MakeRamp(48000 * 2)));
    REQUIRE(streamed);
    const auto instance = mixer.Play(streamed);

    // Mixed no faster than it plays, the worker keeps ahead of it.
    float last = 0.0f;
    for (int i = 0; i < 15; ++i) {
        const auto output = Mix(mixer, 4800);
        last = output[output.size() - 2];
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // A second and a half in, past the first half second decoded before it played.
    CHECK(last == Catch::Approx(static_cast<float>((48000 * 3 / 2 - 1) % 30000) / 32768.0f).margin(1e-3f));
    CHECK(mixer.IsPlaying(instance));
}

TEST_CASE("A disabled audio device plays nothing but stays usable", "[audio]") {
    Spot::AudioDevice audio({.enabled = false});

    CHECK_FALSE(audio.HasOutput());
    CHECK(audio.GetBackendName().empty());
    CHECK(audio.GetSampleRate() == 48000);
    const auto instance = audio.Play(MakeSound(48000, 1, {0.5f}));
    CHECK(instance == Spot::SoundInstance{});
    CHECK_FALSE(audio.IsPlaying(instance));
    audio.SetMasterVolume(0.5f);
    CHECK(audio.GetMasterVolume() == 0.5f);
}

TEST_CASE("The audio device plays sounds through the default output", "[audio]") {
    Spot::AudioDevice audio;
    if (!audio.HasOutput()) {
        SKIP("No audio output is available");
    }
    CHECK_FALSE(audio.GetBackendName().empty());

    // A twentieth of a second of silence, so the test is quiet; it only ends if the output pulls it through the mixer.
    const auto instance = audio.Play(MakeSound(48000, 1, std::vector<float>(2400, 0.0f)));
    CHECK(audio.IsPlaying(instance));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (audio.IsPlaying(instance) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK_FALSE(audio.IsPlaying(instance));
}

TEST_CASE("The mixer lets go of sounds on the game's side, once the audio thread is done with them", "[audio][mixer]") {
    Spot::Detail::Mixer mixer(48000);
    auto sound = MakeSound(48000, 1, {0.1f, 0.2f});
    const std::weak_ptr<const Spot::Sound> watched = sound;
    const auto instance = mixer.Play(std::move(sound));
    // Held while it plays, and after it ended until the game's side hears so.
    Mix(mixer, 4);
    CHECK_FALSE(watched.expired());
    CHECK_FALSE(mixer.IsPlaying(instance));
    CHECK(watched.expired());
}

TEST_CASE("The mixer plays while another thread starts and stops sounds", "[audio][mixer]") {
    Spot::Detail::Mixer mixer(48000);
    const auto sound = MakeSound(48000, 1, std::vector<float>(480, 0.01f));
    std::atomic<bool> done = false;
    std::thread audio([&] {
        std::vector<float> output(256 * 2);
        while (!done.load()) {
            mixer.Mix(output);
        }
    });
    for (int i = 0; i < 2000; ++i) {
        const auto instance = mixer.Play(sound, {.volume = 0.5f});
        mixer.SetVolume(instance, 0.25f);
        if (i % 3 == 0) {
            mixer.Stop(instance);
        }
    }
    done = true;
    audio.join();
    Settle(mixer);
    Settle(mixer);
    CHECK(mixer.GetVoiceCount() == 0);
}
