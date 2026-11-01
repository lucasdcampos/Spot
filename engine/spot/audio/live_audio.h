#pragma once

#include "spot/audio/frame_source.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Spot {

namespace Detail {
class Mixer;
} // namespace Detail

// Audio made while it plays, such as a voice heard over the network: the game writes samples as they come, and a
// playback of it (AudioDevice::Play()) plays them as soon as the mixer gets to them. Mono or stereo, 32-bit float
// samples in [-1, 1] at its own sample rate, which the mixer converts.
//
// A playback that runs out of samples goes on in silence rather than waiting, fading what it last played so it never
// clicks, and plays what comes next at once; it only ends after Close(), once what was written has played. One thread
// writes while the audio thread reads, without locks.
class LiveAudio final : private Detail::FrameSource {
public:
    // Holds up to bufferSeconds of what was written and not played yet.
    explicit LiveAudio(std::uint32_t sampleRate, std::uint32_t channelCount = 1, float bufferSeconds = 0.5f);

    LiveAudio(const LiveAudio&) = delete;
    LiveAudio& operator=(const LiveAudio&) = delete;

    std::uint32_t GetSampleRate() const { return m_sampleRate; }
    std::uint32_t GetChannelCount() const { return m_channels; }

    // Appends interleaved frames, from one thread at a time. Returns how many samples it took: what does not fit, as
    // when nothing plays it, is dropped. Nothing is taken after Close().
    std::size_t Write(std::span<const float> samples);
    // Frames written and not played yet.
    std::size_t GetBufferedFrames() const;
    // Playbacks end once what was written has played.
    void Close();
    bool IsClosed() const { return m_closed.load(std::memory_order_acquire); }

private:
    friend class Detail::Mixer;

    bool ReadFrame(std::array<float, 2>& frame) override;
    bool IsFinished() const override;

    std::uint32_t m_sampleRate;
    std::uint32_t m_channels;
    // In frames.
    std::size_t m_capacity;
    std::vector<float> m_ring;
    // Frames written and read since the start; their difference is what the buffer holds.
    std::atomic<std::uint64_t> m_written = 0;
    std::atomic<std::uint64_t> m_read = 0;
    std::atomic<bool> m_closed = false;
    // The audio thread's: what it last played, which fades out while nothing comes.
    std::array<float, 2> m_last = {};
};

} // namespace Spot
