#include "spot/runtime/runtime_stats.h"

#include "spot/core/time/time.h"
#include "spot/platform/process.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace Spot {
namespace {
std::uint64_t Count(const ProfileFrame& frame, std::string_view name) {
    const double value = Profiler::GetCounter(frame, name);
    // Double cannot represent UINT64_MAX exactly; require a strictly smaller value before conversion.
    return std::isfinite(value) && value > 0.0 && value < static_cast<double>(std::numeric_limits<std::uint64_t>::max())
               ? static_cast<std::uint64_t>(value)
               : 0;
}
} // namespace

RuntimeStats RuntimeStats::FromFrame(const ProfileFrame& frame) {
    RuntimeStats stats;
    stats.frameMilliseconds = std::max(0.0, std::chrono::duration<double, std::milli>(frame.duration).count());
    stats.fps = stats.frameMilliseconds > 0.0 ? 1000.0 / stats.frameMilliseconds : 0.0;
    stats.drawCalls = Count(frame, "Draw calls") + Count(frame, "2D draw calls") + Count(frame, "Shadow draw calls");
    stats.triangles = Count(frame, "Triangles") + Count(frame, "2D triangles");
    stats.bodies = Count(frame, "Bodies");
    stats.voices = Count(frame, "Voices");
    stats.memoryBytes = Count(frame, "Resident memory");
    return stats;
}

RuntimeStats RuntimeStats::Sample() {
    const auto frames = Profiler::GetFrames();
    auto stats = frames.empty() ? RuntimeStats{} : FromFrame(*frames.back());
    if (frames.empty() || !Profiler::IsEnabled()) {
        stats.frameMilliseconds = static_cast<double>(Time::GetUnscaledDeltaTime()) * 1000.0;
        stats.fps = stats.frameMilliseconds > 0.0 ? 1000.0 / stats.frameMilliseconds : 0.0;
        stats.drawCalls = 0;
        stats.triangles = 0;
        stats.bodies = 0;
        stats.voices = 0;
    }
    stats.memoryBytes = Process::GetResidentMemoryBytes();
    return stats;
}

std::string RuntimeStats::Format() const {
    return std::format("{:.0f} FPS | {:.2f} ms | {} draws | {} triangles\n{} bodies | {} voices | {:.1f} MiB resident",
                       fps, frameMilliseconds, drawCalls, triangles, bodies, voices,
                       static_cast<double>(memoryBytes) / (1024.0 * 1024.0));
}

} // namespace Spot
