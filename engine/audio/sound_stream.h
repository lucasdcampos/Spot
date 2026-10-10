#pragma once

// Engine-internal: one playback of a streamed sound. A worker decodes ahead into a ring buffer that the audio thread
// reads, each from its side and without locks: the worker is the only one writing and the audio thread the only one
// reading.

#include "audio/sound.h"
#include "audio/sound_decoder.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Spot::Detail {

class SoundStream {
public:
    // How far ahead the stream decodes.
    static constexpr float BufferSeconds = 0.5f;

    // Null decoder when the sound's data cannot be decoded; see IsValid().
    SoundStream(std::shared_ptr<const Sound> sound, bool loop);

    bool IsValid() const { return m_decoder != nullptr; }

    // Decodes until the buffer is full, or the sound ended, which a looping one never does. Only one thread fills.
    void Fill();
    // The next frame, as left and right; false, taking nothing, when none is decoded yet or the sound ended.
    bool ReadFrame(std::array<float, 2>& frame);
    // Whether the sound ended and every frame of it was read.
    bool IsFinished() const;

private:
    std::shared_ptr<const Sound> m_sound;
    std::unique_ptr<SoundDecoder> m_decoder;
    bool m_loop;
    std::uint32_t m_channels = 1;
    // In frames.
    std::size_t m_capacity = 0;
    std::vector<float> m_ring;
    std::vector<float> m_scratch;
    // Frames written and read since the start; their difference is what the buffer holds.
    std::atomic<std::uint64_t> m_written = 0;
    std::atomic<std::uint64_t> m_read = 0;
    std::atomic<bool> m_ended = false;
};

} // namespace Spot::Detail
