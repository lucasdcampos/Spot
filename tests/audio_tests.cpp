#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string_view>
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
