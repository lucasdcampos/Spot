#pragma once

#include <spot.h>

#include <deque>

namespace Spot::Editor {

struct ProfileAverage {
    std::string_view name;
    double milliseconds = 0.0;
    double callsPerFrame = 0.0;
};

// Owns frame copies so pausing survives the engine ring and game-process restarts.
class ProfilerModel {
public:
    void SetPaused(bool paused) { m_paused = paused; }
    bool IsPaused() const { return m_paused; }
    void Update(std::span<const ProfileFrame* const> frames);
    const std::deque<ProfileFrame>& GetFrames() const { return m_frames; }
    std::vector<ProfileAverage> GetAverages() const;
    void Clear();

private:
    std::deque<ProfileFrame> m_frames;
    bool m_paused = false;
};

} // namespace Spot::Editor
