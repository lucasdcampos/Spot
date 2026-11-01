#include "spot/audio/live_audio.h"

#include <algorithm>

namespace Spot {

namespace {

// Per frame while nothing comes: the last sample falls to a thousandth in about 7 milliseconds at 48 kHz, too fast to
// be heard as a tail and slow enough not to click.
constexpr float SilenceDecay = 0.98f;

} // namespace

LiveAudio::LiveAudio(std::uint32_t sampleRate, std::uint32_t channelCount, float bufferSeconds)
    : m_sampleRate(std::max(sampleRate, 1u)), m_channels(std::clamp(channelCount, 1u, 2u)),
      m_capacity(std::max<std::size_t>(
          static_cast<std::size_t>(std::max(bufferSeconds, 0.0f) * static_cast<float>(m_sampleRate)), 256)) {
    m_ring.resize(m_capacity * m_channels);
}

std::size_t LiveAudio::Write(std::span<const float> samples) {
    if (IsClosed()) {
        return 0;
    }
    const std::uint64_t written = m_written.load(std::memory_order_relaxed);
    const std::uint64_t free = m_capacity - (written - m_read.load(std::memory_order_acquire));
    const std::size_t frames = std::min<std::size_t>(samples.size() / m_channels, static_cast<std::size_t>(free));
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t slot = static_cast<std::size_t>((written + frame) % m_capacity);
        for (std::uint32_t channel = 0; channel < m_channels; ++channel) {
            m_ring[slot * m_channels + channel] = samples[frame * m_channels + channel];
        }
    }
    m_written.store(written + frames, std::memory_order_release);
    return frames * m_channels;
}

std::size_t LiveAudio::GetBufferedFrames() const {
    const std::uint64_t read = m_read.load(std::memory_order_acquire);
    return static_cast<std::size_t>(m_written.load(std::memory_order_acquire) - read);
}

void LiveAudio::Close() {
    m_closed.store(true, std::memory_order_release);
}

bool LiveAudio::ReadFrame(std::array<float, 2>& frame) {
    const std::uint64_t read = m_read.load(std::memory_order_relaxed);
    if (read == m_written.load(std::memory_order_acquire)) {
        if (IsFinished()) {
            return false;
        }
        m_last[0] *= SilenceDecay;
        m_last[1] *= SilenceDecay;
        frame = m_last;
        return true;
    }
    const std::size_t slot = static_cast<std::size_t>(read % m_capacity);
    frame[0] = m_ring[slot * m_channels];
    frame[1] = m_channels == 2 ? m_ring[slot * m_channels + 1] : frame[0];
    m_last = frame;
    m_read.store(read + 1, std::memory_order_release);
    return true;
}

bool LiveAudio::IsFinished() const {
    // Closed is read before the count, so frames written just before closing are never missed.
    return m_closed.load(std::memory_order_acquire) &&
           m_read.load(std::memory_order_relaxed) == m_written.load(std::memory_order_acquire);
}

} // namespace Spot
