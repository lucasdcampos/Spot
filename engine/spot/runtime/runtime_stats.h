#pragma once

#include "spot/core/debug/profiler.h"

#include <string>

namespace Spot {

struct RuntimeStats {
    double fps = 0.0;
    double frameMilliseconds = 0.0;
    std::uint64_t drawCalls = 0;
    std::uint64_t triangles = 0;
    std::uint64_t bodies = 0;
    std::uint64_t voices = 0;
    // Resident process memory, including the engine, assets and backend allocations.
    std::uint64_t memoryBytes = 0;

    static RuntimeStats FromFrame(const ProfileFrame& frame);
    // The latest profiled frame's statistics, with the frame time and memory of now. The modules count bodies and
    // voices for the profiler, so without it they are zero.
    static RuntimeStats Sample();
    std::string Format() const;
};

} // namespace Spot
