#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace Spot {

// Measures frame times. Deltas are clamped so a long stall, such as a breakpoint or a dragged window, does not
// make the game jump ahead in a single frame.
class FrameClock {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using Duration = std::chrono::steady_clock::duration;

    explicit FrameClock(Duration maxDeltaTime = std::chrono::milliseconds(250));

    void Start(TimePoint now);
    void Tick(TimePoint now);

    float GetDeltaTime() const { return m_deltaTime; }
    // Seconds since Start(), as the sum of the clamped deltas.
    double GetTime() const { return m_time; }
    std::uint64_t GetFrameCount() const { return m_frameCount; }

private:
    Duration m_maxDeltaTime;
    TimePoint m_lastTick;
    float m_deltaTime = 0.0f;
    double m_time = 0.0;
    std::uint64_t m_frameCount = 0;
};

class FrameLimiter {
public:
    // A rate of 0 means unlimited.
    explicit FrameLimiter(int maxFrameRate = 0);

    // Called before each frame; returns how long to wait so frames start no more often than the limit.
    // A late frame reschedules from its own start instead of rushing the next frames to catch up.
    FrameClock::Duration GetWaitTime(FrameClock::TimePoint now);

private:
    FrameClock::Duration m_period;
    std::optional<FrameClock::TimePoint> m_nextFrame;
};

// Global access to the frame timing of the running application, e.g. Spot::Time::GetDeltaTime().
namespace Time {

// The application points this at the clock it ticks every frame. Without a clock every query returns 0.
void SetClock(const FrameClock* clock);

float GetDeltaTime();
double GetTime();
std::uint64_t GetFrameCount();

} // namespace Time

} // namespace Spot
