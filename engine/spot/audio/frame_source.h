#pragma once

#include <array>

// Engine-internal: what a voice of the mixer reads frames from as they come, rather than from a sound in memory:
// streamed sounds and live audio.
namespace Spot::Detail {

class FrameSource {
public:
    virtual ~FrameSource() = default;

    // The next frame, as left and right; false, taking nothing, when none is ready yet or the source ended. Only the
    // audio thread reads.
    virtual bool ReadFrame(std::array<float, 2>& frame) = 0;
    // Whether the source ended and every frame of it was read.
    virtual bool IsFinished() const = 0;
};

} // namespace Spot::Detail
