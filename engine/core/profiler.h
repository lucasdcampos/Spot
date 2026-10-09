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
    // How many scopes it is nested in: 0 for the frame's outermost scopes.
    std::uint32_t depth = 0;
    // From the start of its frame.
    std::chrono::nanoseconds start{};
    std::chrono::nanoseconds duration{};
};

struct ProfileFrame {
    // Counts frames from the first one profiled.
    std::uint64_t number = 0;
    std::chrono::nanoseconds duration{};
    // In the order their scopes began, each followed by the scopes nested in it, so depths describe the tree.
    std::vector<ProfileSample> samples;
    // Scopes left out once the frame had Profiler::MaxSamplesPerFrame.
    std::size_t droppedSamples = 0;
};

// Records how long the scopes marked with SPOT_PROFILE_SCOPE() take on the game thread, the one that begins frames,
// for the last frames. It is cheap enough to stay on in Release: a scope reads the clock twice, and frames reuse
// their memory.
//
//     void Spawner::OnUpdate(float deltaTime) {
//         SPOT_PROFILE_SCOPE("Spawner::OnUpdate");
//         ...
//     }
//
// Scopes on other threads, and outside of frames, are not recorded.
namespace Profiler {

inline constexpr std::size_t MaxFrames = 240;
inline constexpr std::size_t MaxSamplesPerFrame = 8192;

// Frames are begun and ended by the application; code that runs its own loop calls these. Beginning a frame ends
// the one in progress, and the thread that begins frames is the one profiled.
void BeginFrame();
void EndFrame();

bool IsEnabled();
// While disabled, scopes are not recorded and frames are not kept.
void SetEnabled(bool enabled);

// The last frames that ended, oldest first, at most MaxFrames. They stay valid until the next frame begins.
std::vector<const ProfileFrame*> GetFrames();
// Forgets every frame.
void Clear();

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
