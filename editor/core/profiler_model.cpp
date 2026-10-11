#include "core/profiler_model.h"

#include <algorithm>
#include <map>

namespace Spot::Editor {

void ProfilerModel::Clear() {
    m_frames.clear();
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
        if (m_frames.size() > Profiler::MaxFrames) {
            m_frames.pop_front();
        }
    }
}

std::vector<ProfileAverage> ProfilerModel::GetAverages() const {
    std::map<std::string_view, ProfileAverage> totals;
    for (const auto& frame : m_frames) {
        for (const auto& sample : frame.samples) {
            auto& total = totals[sample.name];
            total.name = sample.name;
            total.milliseconds += std::chrono::duration<double, std::milli>(sample.duration).count();
            total.callsPerFrame += 1.0;
        }
    }
    std::vector<ProfileAverage> result;
    if (!m_frames.empty()) {
        for (auto& [name, total] : totals) {
            total.milliseconds /= static_cast<double>(m_frames.size());
            total.callsPerFrame /= static_cast<double>(m_frames.size());
            result.push_back(total);
        }
    }
    std::ranges::sort(result,
                      [](const auto& first, const auto& second) { return first.milliseconds > second.milliseconds; });
    return result;
}

} // namespace Spot::Editor
