#include "spot/core/timer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Spot {

InvokeId TimerQueue::Add(float delay, float interval, std::function<void()> callback) {
    if (!callback || !std::isfinite(delay) || delay < 0.0f || m_nextId == 0) {
        return InvokeId::Invalid;
    }
    const auto id = static_cast<InvokeId>(m_nextId++);
    m_timers.push_back({id, delay, interval, std::move(callback)});
    return id;
}

InvokeId TimerQueue::Invoke(float delay, std::function<void()> callback) {
    return Add(delay, 0.0f, std::move(callback));
}

InvokeId TimerQueue::InvokeRepeating(float delay, float interval, std::function<void()> callback) {
    if (!std::isfinite(interval) || interval <= 0.0f) {
        return InvokeId::Invalid;
    }
    return Add(delay, interval, std::move(callback));
}

bool TimerQueue::Cancel(InvokeId id) {
    return std::erase_if(m_timers, [id](const Timer& timer) { return timer.id == id; }) != 0;
}

void TimerQueue::CancelAll() {
    m_timers.clear();
}

bool TimerQueue::IsPending(InvokeId id) const {
    return std::ranges::any_of(m_timers, [id](const Timer& timer) { return timer.id == id; });
}

float TimerQueue::GetTimeLeft(InvokeId id) const {
    const auto found = std::ranges::find(m_timers, id, &Timer::id);
    return found == m_timers.end() ? 0.0f : static_cast<float>(std::max(0.0, found->timeLeft));
}

void TimerQueue::Update(float deltaTime, const std::function<bool()>& shouldContinue) {
    if (m_updating || !std::isfinite(deltaTime) || deltaTime <= 0.0f) {
        return;
    }
    m_updating = true;
    struct Reset {
        bool& updating;
        ~Reset() { updating = false; }
    } reset{m_updating};
    std::vector<InvokeId> pending;
    pending.reserve(m_timers.size());
    for (auto& timer : m_timers) {
        timer.timeLeft -= deltaTime;
        pending.push_back(timer.id);
    }
    for (const auto id : pending) {
        if (shouldContinue && !shouldContinue()) {
            break;
        }
        auto found = std::ranges::find(m_timers, id, &Timer::id);
        if (found == m_timers.end() || found->timeLeft > 0.0) {
            continue;
        }
        auto callback = found->callback;
        if (found->interval > 0.0f) {
            found->timeLeft = found->interval + std::fmod(found->timeLeft, found->interval);
        } else {
            m_timers.erase(found);
        }
        callback();
    }
}

} // namespace Spot
