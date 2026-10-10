#include "core/profiler.h"

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

namespace Spot {

namespace {

using Clock = std::chrono::steady_clock;

struct ProfilerState {
    // One more than kept, so the frame being recorded never overwrites one that GetFrames() returns.
    std::array<ProfileFrame, Profiler::MaxFrames + 1> frames;
    // The slot of the frame being recorded, or of the last one that ended.
    std::size_t current = 0;
    std::size_t completed = 0;
    std::uint64_t nextNumber = 0;
    bool recording = false;
    bool enabled = true;
    Clock::time_point frameStart;
    std::thread::id thread;
    // The samples of the open scopes, innermost last.
    std::vector<std::uint32_t> open;
};

ProfilerState& GetState() {
    // Allocated on first use, since it is large, and never freed, so scopes in static destructors stay safe.
    static ProfilerState* state = new ProfilerState();
    return *state;
}

} // namespace

namespace Profiler {

void BeginFrame() {
    ProfilerState& state = GetState();
    if (state.recording) {
        EndFrame();
    }
    if (!state.enabled) {
        return;
    }
    state.current = state.completed == 0 && state.nextNumber == 0 ? 0 : (state.current + 1) % state.frames.size();
    ProfileFrame& frame = state.frames[state.current];
    frame.number = state.nextNumber++;
    frame.duration = {};
    frame.samples.clear();
    frame.droppedSamples = 0;
    frame.counters.clear();
    if (frame.samples.capacity() == 0) {
        frame.samples.reserve(256);
    }
    state.open.clear();
    state.thread = std::this_thread::get_id();
    state.recording = true;
    state.frameStart = Clock::now();
}

void EndFrame() {
    ProfilerState& state = GetState();
    if (!state.recording) {
        return;
    }
    ProfileFrame& frame = state.frames[state.current];
    const auto end = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - state.frameStart);
    // Scopes still open end with the frame.
    for (const std::uint32_t index : state.open) {
        frame.samples[index].duration = end - frame.samples[index].start;
    }
    state.open.clear();
    frame.duration = end;
    state.recording = false;
    state.completed = std::min(state.completed + 1, MaxFrames);
}

bool IsEnabled() {
    return GetState().enabled;
}

void SetEnabled(bool enabled) {
    ProfilerState& state = GetState();
    if (!enabled && state.recording) {
        // The frame in progress is dropped rather than kept with part of its scopes.
        state.recording = false;
        state.current = (state.current + state.frames.size() - 1) % state.frames.size();
    }
    state.enabled = enabled;
}

std::vector<const ProfileFrame*> GetFrames() {
    const ProfilerState& state = GetState();
    std::vector<const ProfileFrame*> frames;
    frames.reserve(state.completed);
    // The last ended frame is the current slot, or the one before it while a frame is being recorded.
    const std::size_t size = state.frames.size();
    const std::size_t newest = state.recording ? (state.current + size - 1) % size : state.current;
    for (std::size_t i = state.completed; i-- > 0;) {
        frames.push_back(&state.frames[(newest + size - i) % size]);
    }
    return frames;
}

void Clear() {
    ProfilerState& state = GetState();
    state.recording = false;
    state.completed = 0;
    state.nextNumber = 0;
    state.current = 0;
    state.open.clear();
}

void Count(std::string_view name, double amount) {
    ProfilerState& state = GetState();
    if (!state.recording || state.thread != std::this_thread::get_id()) {
        return;
    }
    std::vector<ProfileCounter>& counters = state.frames[state.current].counters;
    for (ProfileCounter& counter : counters) {
        if (counter.name == name) {
            counter.value += amount;
            return;
        }
    }
    counters.push_back({name, amount});
}

double GetCounter(const ProfileFrame& frame, std::string_view name) {
    for (const ProfileCounter& counter : frame.counters) {
        if (counter.name == name) {
            return counter.value;
        }
    }
    return 0.0;
}

std::string_view Intern(std::string_view name) {
    static std::mutex mutex;
    // Never freed, so the names outlive every frame that refers to them.
    static auto* names = new std::unordered_set<std::string>();
    const std::lock_guard lock(mutex);
    return *names->emplace(name).first;
}

} // namespace Profiler

namespace Detail {

ProfileScope::ProfileScope(std::string_view name) {
    ProfilerState& state = GetState();
    if (!state.recording || state.thread != std::this_thread::get_id()) {
        return;
    }
    ProfileFrame& frame = state.frames[state.current];
    if (frame.samples.size() >= Profiler::MaxSamplesPerFrame) {
        ++frame.droppedSamples;
        return;
    }
    m_frame = frame.number;
    m_index = static_cast<std::uint32_t>(frame.samples.size());
    m_recording = true;
    frame.samples.push_back({
        .name = name,
        .depth = static_cast<std::uint32_t>(state.open.size()),
        .start = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - state.frameStart),
    });
    state.open.push_back(m_index);
}

ProfileScope::~ProfileScope() {
    if (!m_recording) {
        return;
    }
    ProfilerState& state = GetState();
    ProfileFrame& frame = state.frames[state.current];
    // A scope that outlived its frame was ended with it.
    if (!state.recording || frame.number != m_frame) {
        return;
    }
    ProfileSample& sample = frame.samples[m_index];
    sample.duration =
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - state.frameStart) - sample.start;
    if (!state.open.empty() && state.open.back() == m_index) {
        state.open.pop_back();
    }
}

} // namespace Detail

} // namespace Spot
