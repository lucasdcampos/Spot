#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace Spot {

enum class InvokeId : std::uint64_t { Invalid = 0 };

// Advances only when Update receives positive simulation time. Missed repetitions are coalesced into one call
// per update, and callbacks added while updating wait until the next update.
class TimerQueue {
public:
    InvokeId Invoke(float delay, std::function<void()> callback);
    InvokeId InvokeRepeating(float delay, float interval, std::function<void()> callback);
    bool Cancel(InvokeId id);
    void CancelAll();
    bool IsPending(InvokeId id) const;
    float GetTimeLeft(InvokeId id) const;
    void Update(float deltaTime, const std::function<bool()>& shouldContinue = {});

private:
    struct Timer {
        InvokeId id;
        double timeLeft;
        float interval;
        std::function<void()> callback;
    };
    InvokeId Add(float delay, float interval, std::function<void()> callback);

    std::vector<Timer> m_timers;
    std::uint64_t m_nextId = 1;
    bool m_updating = false;
};

} // namespace Spot
