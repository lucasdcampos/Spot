#pragma once

#include "spot/core/debug/profiler.h"

#include <span>
#include <string>

namespace Spot {

class Scene;
class AudioDevice;

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
    static RuntimeStats Sample(std::span<Scene* const> scenes, const AudioDevice* audio = nullptr);
    std::string Format() const;
};

} // namespace Spot
