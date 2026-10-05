#include "log_capture.h"

#include "audio/mixer.h"

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
                               std::initializer_list<std::int16_t> samples) {
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

TEST_CASE("The mixer applies volume, master volume and pan", "[audio][mixer]") {
    Spot::Detail::Mixer mixer(48000);
    const auto sound = MakeSound(48000, 1, {0.5f, 0.5f, 0.5f, 0.5f});
    const auto instance = mixer.Play(sound, {.volume = 0.5f, .pan = -1.0f, .loop = true});

    CHECK(Mix(mixer, 1) == std::vector<float>{0.25f, 0.0f});

    mixer.SetPan(instance, 1.0f);
    CHECK(Mix(mixer, 1) == std::vector<float>{0.0f, 0.25f});

    mixer.SetPan(instance, 0.5f);
    mixer.SetVolume(instance, 1.0f);
    mixer.SetMasterVolume(0.5f);
    CHECK(mixer.GetMasterVolume() == 0.5f);
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

TEST_CASE("Paused sounds are silent and resume where they stopped", "[audio][mixer]") {
    Spot::Detail::Mixer mixer(48000);
    const auto instance = mixer.Play(MakeSound(48000, 1, {0.1f, 0.2f}));

    CHECK(Mix(mixer, 1) == std::vector<float>{0.1f, 0.1f});
    mixer.SetPaused(instance, true);
    CHECK(mixer.IsPaused(instance));
    CHECK(mixer.IsPlaying(instance));
    CHECK(Mix(mixer, 1) == std::vector<float>{0.0f, 0.0f});
    mixer.SetPaused(instance, false);
    CHECK(Mix(mixer, 1) == std::vector<float>{0.2f, 0.2f});
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
