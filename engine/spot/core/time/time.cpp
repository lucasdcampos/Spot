#include "spot/core/time/time.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Spot {

FrameClock::FrameClock(Duration maxDeltaTime) : m_maxDeltaTime(std::max(Duration::zero(), maxDeltaTime)) {
}

bool FrameClock::SetTimeScale(float scale) {
    if (!std::isfinite(scale) || scale < 0.0f) {
        return false;
    }
    m_timeScale = scale;
    m_deltaTime = static_cast<float>(std::min(static_cast<double>(m_unscaledDeltaTime) * scale,
                                              static_cast<double>(std::numeric_limits<float>::max())));
    return true;
}

void FrameClock::Start(TimePoint now) {
    m_lastTick = now;
    m_deltaTime = 0.0f;
    m_unscaledDeltaTime = 0.0f;
    m_unscaledTime = 0.0;
    m_time = 0.0;
    m_frameCount = 0;
}

void FrameClock::Tick(TimePoint now) {
    const auto delta = std::clamp(now - m_lastTick, Duration::zero(), m_maxDeltaTime);
    m_lastTick = now;
    m_unscaledDeltaTime = std::chrono::duration<float>(delta).count();
    m_unscaledTime += std::chrono::duration<double>(delta).count();
    SetTimeScale(m_timeScale);
    m_time += std::chrono::duration<double>(delta).count() * m_timeScale;
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

FrameClock* s_clock = nullptr;
float s_timeScale = 1.0f;

} // namespace

void SetClock(FrameClock* clock) {
    s_clock = clock;
    if (clock) {
        clock->SetTimeScale(s_timeScale);
    }
}

float GetTimeScale() {
    return s_timeScale;
}

bool SetTimeScale(float scale) {
    if (!std::isfinite(scale) || scale < 0.0f) {
        return false;
    }
    s_timeScale = scale;
    return !s_clock || s_clock->SetTimeScale(scale);
}

float GetUnscaledDeltaTime() {
    return s_clock ? s_clock->GetUnscaledDeltaTime() : 0.0f;
}

double GetUnscaledTime() {
    return s_clock ? s_clock->GetUnscaledTime() : 0.0;
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
