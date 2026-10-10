#pragma once

// Engine-internal: decodes a streamed sound's data a few frames at a time, one decoder per playback.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace Spot::Detail {

class SoundDecoder {
public:
    // WAV or Ogg Vorbis data, which must outlive the decoder. Returns null, after logging why, when it cannot be
    // decoded or is neither mono nor stereo.
    static std::unique_ptr<SoundDecoder> Open(std::span<const std::byte> encoded);

    virtual ~SoundDecoder() = default;

    virtual std::uint32_t GetSampleRate() const = 0;
    virtual std::uint32_t GetChannelCount() const = 0;
    virtual std::uint64_t GetFrameCount() const = 0;
    // Decodes up to as many whole frames as the output holds, interleaved. Returns how many; 0 once it reached the end.
    virtual std::size_t Decode(std::span<float> output) = 0;
    // Back to the first frame, as looping needs.
    virtual bool Rewind() = 0;
};

} // namespace Spot::Detail
