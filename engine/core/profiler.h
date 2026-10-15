#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Spot {

// A scope timed by the profiler.
struct ProfileSample {
    // A string literal, or a name from Profiler::Intern().
    std::string_view name;
    // How many scopes it is nested in, on its thread: 0 for the outermost ones.
    std::uint32_t depth = 0;
    // From the start of its frame.
    std::chrono::nanoseconds start{};
    std::chrono::nanoseconds duration{};
    // 0 for the thread that begins frames; other threads, such as the job system's workers, are numbered from 1 in the
    // order they first recorded a scope.
    std::uint32_t thread = 0;
};

// A number a frame counted, such as how many meshes it drew.
struct ProfileCounter {
    // A string literal, or a name from Profiler::Intern().
    std::string_view name;
    double value = 0.0;
};

struct ProfileFrame {
    // Counts frames from the first one profiled.
    std::uint64_t number = 0;
    std::chrono::nanoseconds duration{};
    // Thread by thread, the frame's thread first; on each, in the order their scopes began, each followed by the
    // scopes nested in it, so depths describe the tree.
    std::vector<ProfileSample> samples;
    // Scopes left out once the frame had Profiler::MaxSamplesPerFrame.
    std::size_t droppedSamples = 0;
    // In the order they were first counted in the frame.
    std::vector<ProfileCounter> counters;
};

// Records how long the scopes marked with SPOT_PROFILE_SCOPE() take, for the last frames: on the thread that begins
// frames, and on any other thread, such as the job system's workers, while a frame is recorded. It is cheap enough to
// stay on in Release: a scope reads the clock twice, and frames reuse their memory.
//
//     void Spawner::OnUpdate(float deltaTime) {
//         SPOT_PROFILE_SCOPE("Spawner::OnUpdate");
//         ...
//     }
//
// Scopes outside of frames are not recorded. Other threads' scopes join the frame when it ends; those still open then
// end with it.
namespace Profiler {

inline constexpr std::size_t MaxFrames = 240;
inline constexpr std::size_t MaxSamplesPerFrame = 8192;

// Frames are begun and ended by the application; code that runs its own loop calls these. Beginning a frame ends
// the one in progress, and the thread that begins frames is the frame's thread.
void BeginFrame();
void EndFrame();

bool IsEnabled();
// While disabled, scopes are not recorded and frames are not kept.
void SetEnabled(bool enabled);

// The last frames that ended, oldest first, at most MaxFrames. They stay valid until the next frame begins.
std::vector<const ProfileFrame*> GetFrames();
// Forgets every frame.
void Clear();
// Adds to the frame's counter of that name, which starts at 0 each frame, so code that runs several times a frame,
// such as a renderer drawing several views, adds up. Counts outside of frames, or off the frame's thread, are
// dropped.
void Count(std::string_view name, double amount = 1.0);
// The frame's counter of that name; 0 when it counted none.
double GetCounter(const ProfileFrame& frame, std::string_view name);

// A copy of the name that lasts as long as the process, for scopes named at run time, such as after a type. Each
// distinct name is kept once.
std::string_view Intern(std::string_view name);

} // namespace Profiler

namespace Detail {

class ProfileScope {
public:
    explicit ProfileScope(std::string_view name);
    ~ProfileScope();

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    std::uint64_t m_frame = 0;
    std::uint32_t m_index = 0;
    bool m_recording = false;
    // Recorded in another thread's samples, which join the frame when it ends.
    bool m_otherThread = false;
};

} // namespace Detail

} // namespace Spot

#define SPOT_PROFILE_CONCAT_INNER(a, b) a##b
#define SPOT_PROFILE_CONCAT(a, b) SPOT_PROFILE_CONCAT_INNER(a, b)
// Times the rest of the enclosing scope under the name, a string literal or a name from Profiler::Intern().
#define SPOT_PROFILE_SCOPE(name)                                                                                       \
    const ::Spot::Detail::ProfileScope SPOT_PROFILE_CONCAT(spotProfileScope, __LINE__) {                               \
        name                                                                                                           \
    }
