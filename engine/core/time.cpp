#include "core/time.h"

#include <algorithm>

namespace Spot {

FrameClock::FrameClock(Duration maxDeltaTime) : m_maxDeltaTime(maxDeltaTime) {
}

void FrameClock::Start(TimePoint now) {
    m_lastTick = now;
    m_deltaTime = 0.0f;
    m_time = 0.0;
    m_frameCount = 0;
}

void FrameClock::Tick(TimePoint now) {
    const auto delta = std::min(now - m_lastTick, m_maxDeltaTime);
    m_lastTick = now;
    m_deltaTime = std::chrono::duration<float>(delta).count();
    m_time += std::chrono::duration<double>(delta).count();
    ++m_frameCount;
}

FrameLimiter::FrameLimiter(int maxFrameRate)
    : m_period(maxFrameRate > 0
                   ? std::chrono::duration_cast<FrameClock::Duration>(std::chrono::duration<double>(1.0 / maxFrameRate))
                   : FrameClock::Duration::zero()) {
}

FrameClock::Duration FrameLimiter::GetWaitTime(FrameClock::TimePoint now) {
    if (m_period == FrameClock::Duration::zero()) {
        return FrameClock::Duration::zero();
    }

    const auto frameStart = m_nextFrame ? std::max(*m_nextFrame, now) : now;
    m_nextFrame = frameStart + m_period;
    return frameStart - now;
}

namespace Time {

namespace {

const FrameClock* s_clock = nullptr;

} // namespace

void SetClock(const FrameClock* clock) {
    s_clock = clock;
}

float GetDeltaTime() {
    return s_clock ? s_clock->GetDeltaTime() : 0.0f;
}

double GetTime() {
    return s_clock ? s_clock->GetTime() : 0.0;
}

std::uint64_t GetFrameCount() {
    return s_clock ? s_clock->GetFrameCount() : 0;
}

} // namespace Time

} // namespace Spot
