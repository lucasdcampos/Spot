#include "audio/sound_stream.h"

#include <algorithm>
#include <utility>

namespace Spot::Detail {

namespace {

constexpr std::size_t ChunkFrames = 1024;

} // namespace

SoundStream::SoundStream(std::shared_ptr<const Sound> sound, bool loop)
    : m_sound(std::move(sound)), m_decoder(SoundDecoder::Open(m_sound->GetEncodedData())), m_loop(loop) {
    if (!m_decoder) {
        return;
    }
    m_channels = m_decoder->GetChannelCount();
    m_capacity =
        std::max(static_cast<std::size_t>(BufferSeconds * static_cast<float>(m_decoder->GetSampleRate())), ChunkFrames);
    m_ring.resize(m_capacity * m_channels);
    m_scratch.resize(ChunkFrames * m_channels);
}

void SoundStream::Fill() {
    if (!m_decoder) {
        return;
    }
    while (!m_ended.load(std::memory_order_relaxed)) {
        const std::uint64_t written = m_written.load(std::memory_order_relaxed);
        const std::uint64_t free = m_capacity - (written - m_read.load(std::memory_order_acquire));
        if (free == 0) {
            return;
        }
        const std::size_t wanted = std::min<std::size_t>(ChunkFrames, static_cast<std::size_t>(free));
        std::size_t decoded = m_decoder->Decode(std::span(m_scratch).first(wanted * m_channels));
        if (decoded == 0) {
            // A looping sound goes on from its start; a decoder that cannot rewind, or a sound with no frames, ends.
            if (!m_loop || !m_decoder->Rewind() ||
                (decoded = m_decoder->Decode(std::span(m_scratch).first(wanted * m_channels))) == 0) {
                m_ended.store(true, std::memory_order_release);
                return;
            }
        }
        for (std::size_t frame = 0; frame < decoded; ++frame) {
            const std::size_t slot = static_cast<std::size_t>((written + frame) % m_capacity);
            for (std::uint32_t channel = 0; channel < m_channels; ++channel) {
                m_ring[slot * m_channels + channel] = m_scratch[frame * m_channels + channel];
            }
        }
        m_written.store(written + decoded, std::memory_order_release);
    }
}

bool SoundStream::ReadFrame(std::array<float, 2>& frame) {
    const std::uint64_t read = m_read.load(std::memory_order_relaxed);
    if (read == m_written.load(std::memory_order_acquire)) {
        return false;
    }
    const std::size_t slot = static_cast<std::size_t>(read % m_capacity);
    frame[0] = m_ring[slot * m_channels];
    frame[1] = m_channels == 2 ? m_ring[slot * m_channels + 1] : frame[0];
    m_read.store(read + 1, std::memory_order_release);
    return true;
}

bool SoundStream::IsFinished() const {
    // The end is read before the count, so frames written just before it are never missed.
    return m_ended.load(std::memory_order_acquire) &&
           m_read.load(std::memory_order_relaxed) == m_written.load(std::memory_order_acquire);
}

} // namespace Spot::Detail
