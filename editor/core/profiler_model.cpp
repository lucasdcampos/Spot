#include "core/profiler_model.h"

#include <algorithm>
#include <map>

namespace Spot::Editor {

void ProfilerModel::Clear() {
    m_frames.clear();
    m_totals.clear();
}

void ProfilerModel::Update(std::span<const ProfileFrame* const> frames) {
    if (m_paused) {
        return;
    }
    const auto last = std::ranges::find_if(frames.rbegin(), frames.rend(),
                                           [](const ProfileFrame* frame) { return frame != nullptr; });
    if (last != frames.rend() && !m_frames.empty() && (*last)->number < m_frames.back().number) {
        Clear();
    }
    for (const auto* frame : frames) {
        if (!frame || (!m_frames.empty() && frame->number <= m_frames.back().number)) {
            continue;
        }
        m_frames.push_back(*frame);
        // Samples supplied by tools need the same stable names as engine samples.
        for (auto& sample : m_frames.back().samples) {
            sample.name = Profiler::Intern(sample.name);
        }
        for (auto& counter : m_frames.back().counters) {
            counter.name = Profiler::Intern(counter.name);
        }
        Accumulate(m_frames.back(), 1);
        if (m_frames.size() > Profiler::MaxFrames) {
            Accumulate(m_frames.front(), -1);
            m_frames.pop_front();
        }
    }
}

void ProfilerModel::Accumulate(const ProfileFrame& frame, int direction) {
    for (const auto& sample : frame.samples) {
        auto& total = m_totals[sample.name];
        total.milliseconds +=
            static_cast<double>(direction) * std::chrono::duration<double, std::milli>(sample.duration).count();
        total.calls += direction;
        if (total.calls == 0) {
            m_totals.erase(sample.name);
        }
    }
}

std::vector<ProfileAverage> ProfilerModel::GetAverages() const {
    std::vector<ProfileAverage> result;
    if (!m_frames.empty()) {
        const double count = static_cast<double>(m_frames.size());
        for (const auto& [name, total] : m_totals) {
            result.push_back(
                {name, std::max(0.0, total.milliseconds) / count, static_cast<double>(total.calls) / count});
        }
    }
    std::ranges::sort(result,
                      [](const auto& first, const auto& second) { return first.milliseconds > second.milliseconds; });
    return result;
}

} // namespace Spot::Editor
