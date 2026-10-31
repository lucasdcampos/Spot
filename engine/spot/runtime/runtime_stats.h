#pragma once

#include "spot/core/debug/profiler.h"

#include <functional>
#include <string>
#include <vector>

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
    // What the StatsSections alive when sampled describe, in the order they were made; empty ones are left out.
    std::vector<std::string> sections;

    static RuntimeStats FromFrame(const ProfileFrame& frame);
    // The latest profiled frame's statistics, with the frame time and memory of now. The modules count bodies and
    // voices for the profiler, so without it they are zero.
    static RuntimeStats Sample();
    std::string Format() const;
};

// Lines a system adds to the statistics overlay while it exists, as networking does for its session: Sample() asks
// each for its text, which may be empty for nothing to show now. Made and destroyed on the main thread.
class StatsSection {
public:
    explicit StatsSection(std::function<std::string()> describe);
    ~StatsSection();

    StatsSection(const StatsSection&) = delete;
    StatsSection& operator=(const StatsSection&) = delete;

private:
    friend struct RuntimeStats;

    std::function<std::string()> m_describe;
};

} // namespace Spot
