#pragma once

#include <algorithm>

namespace Spot {

// How often a peer may have something done: up to `burst` at once, and `rate` a second in the long run. For what a
// server does for each message a client may send as fast as it likes.
class NetRateLimit {
public:
    // Whether it may happen now, by a clock in seconds, as NetworkSession::GetTime(); counts it when it may.
    bool Take(double now, float rate, float burst) {
        const double earned = m_started ? std::max(now - m_last, 0.0) * rate : static_cast<double>(burst);
        m_tokens = std::min(m_tokens + earned, static_cast<double>(burst));
        m_last = now;
        m_started = true;
        if (m_tokens < 1.0) {
            return false;
        }
        m_tokens -= 1.0;
        return true;
    }

private:
    double m_tokens = 0.0;
    double m_last = 0.0;
    bool m_started = false;
};

} // namespace Spot
