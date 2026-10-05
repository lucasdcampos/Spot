#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace Spot {

// Decoded audio in CPU memory: 32-bit float samples in [-1, 1], mono or stereo, with the channels of each frame
// interleaved. Sounds never change, so one sound can play many times at once.
class Sound {
public:
    Sound(std::uint32_t sampleRate, std::uint32_t channelCount, std::vector<float> samples);

    // Reads WAV and Ogg Vorbis. Returns null, after logging why, on failure.
    static std::shared_ptr<Sound> Load(const std::filesystem::path& path);
    static std::shared_ptr<Sound> LoadFromMemory(std::span<const std::byte> data);

    std::uint32_t GetSampleRate() const { return m_sampleRate; }
    std::uint32_t GetChannelCount() const { return m_channelCount; }
    std::uint64_t GetFrameCount() const { return m_samples.size() / m_channelCount; }
    // In seconds.
    float GetDuration() const { return static_cast<float>(GetFrameCount()) / static_cast<float>(m_sampleRate); }
    std::span<const float> GetSamples() const { return m_samples; }

private:
    std::uint32_t m_sampleRate;
    std::uint32_t m_channelCount;
    std::vector<float> m_samples;
};

} // namespace Spot
