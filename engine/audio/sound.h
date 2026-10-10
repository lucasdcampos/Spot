#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace Spot {

// Audio a sound plays: 32-bit float samples in [-1, 1], mono or stereo, with the channels of each frame interleaved.
// Sounds never change, so one sound can play many times at once.
//
// Most sounds are decoded in CPU memory. Streamed sounds, for music and long ambience, keep their WAV or Ogg Vorbis
// data encoded instead, and each playback decodes it as it goes, so they take a fraction of the memory.
class Sound {
public:
    Sound(std::uint32_t sampleRate, std::uint32_t channelCount, std::vector<float> samples);

    // Reads WAV and Ogg Vorbis. Returns null, after logging why, on failure.
    static std::shared_ptr<Sound> Load(const std::filesystem::path& path);
    static std::shared_ptr<Sound> LoadFromMemory(std::span<const std::byte> data);
    // A streamed sound of WAV or Ogg Vorbis data. Returns null, after logging why, when it cannot be decoded.
    static std::shared_ptr<Sound> LoadStream(const std::filesystem::path& path);
    static std::shared_ptr<Sound> CreateStream(std::vector<std::byte> encoded);

    std::uint32_t GetSampleRate() const { return m_sampleRate; }
    std::uint32_t GetChannelCount() const { return m_channelCount; }
    std::uint64_t GetFrameCount() const { return m_frameCount; }
    // In seconds.
    float GetDuration() const { return static_cast<float>(GetFrameCount()) / static_cast<float>(m_sampleRate); }
    // Empty for streamed sounds.
    std::span<const float> GetSamples() const { return m_samples; }

    bool IsStreamed() const { return !m_encoded.empty(); }
    // A streamed sound's WAV or Ogg Vorbis data; empty for others.
    std::span<const std::byte> GetEncodedData() const { return m_encoded; }

private:
    Sound(std::uint32_t sampleRate, std::uint32_t channelCount, std::uint64_t frameCount,
          std::vector<std::byte> encoded);

    std::uint32_t m_sampleRate;
    std::uint32_t m_channelCount;
    std::uint64_t m_frameCount;
    std::vector<float> m_samples;
    std::vector<std::byte> m_encoded;
};

} // namespace Spot
