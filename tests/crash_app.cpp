// Crashes the way it is told, after installing the crash handler, for the crash tests to look at what it left:
//   spot_crash_app <access|pure|exception|abort|overflow|assert> <log file> <report folder>
#include <spot.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef _MSC_VER
#define SPOT_NO_INLINE __declspec(noinline)
#else
#define SPOT_NO_INLINE __attribute__((noinline))
#endif

namespace {

// Volatile and from outside, so compilers neither see the crashes coming nor optimize them away.
volatile int s_zero = 0;

SPOT_NO_INLINE void CrashByAccess() {
    volatile int* pointer = reinterpret_cast<volatile int*>(static_cast<std::uintptr_t>(s_zero));
    *pointer = 42;
}

class Base {
public:
    Base() { CallBehavior(); }
    virtual ~Base() = default;
    // Called through a function the constructor calls, when only Base's part exists.
    void CallBehavior() { Behavior(); }
    virtual void Behavior() = 0;
};

class Derived final : public Base {
public:
    void Behavior() override {}
};

SPOT_NO_INLINE void CrashByPureCall() {
    const auto derived = std::make_unique<Derived>();
}

SPOT_NO_INLINE void CrashByException() {
    throw std::runtime_error("nobody catches this");
}

// Recurses as long as it is told to, which is forever.
SPOT_NO_INLINE int CrashByOverflow(int depth) {
    volatile char buffer[4096];
    buffer[0] = static_cast<char>(depth);
    return depth < 0 ? buffer[0] : CrashByOverflow(depth + 1 + s_zero) + buffer[0];
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--emit-profile") {
        if (Spot::Process::GetEnvironmentValue("SPOT_PROFILE_STREAM") != "1") {
            return 3;
        }
        for (std::uint64_t i = 1; i <= 250; ++i) {
            Spot::ProfileFrame frame;
            frame.number = i;
            frame.duration = std::chrono::milliseconds(16);
            frame.samples = {{"Remote physics", 0, {}, std::chrono::milliseconds(12)}};
            frame.counters = {{"Bodies", static_cast<double>(i)}};
            Spot::Log::Info("{}", Spot::EncodeProfileFrame(frame));
        }
        Spot::Log::Info("Profile fixture finished");
        return 0;
    }
    if (argc != 4) {
        std::fputs("Usage: spot_crash_app <access|pure|exception|abort|overflow|assert> <log> <reports>\n", stderr);
        return 2;
    }
#ifdef _WIN32
    // The crash is reported already; Windows' own report would only hold the test.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    const auto sink = std::make_shared<Spot::FileSink>(std::filesystem::path(argv[2]));
    Spot::Log::AddSink(sink);
    Spot::InstallCrashHandler({.applicationName = "Crash App", .reportDirectory = std::filesystem::path(argv[3])});
    Spot::Log::Info("About to crash");

    const std::string_view mode = argv[1];
    if (mode == "access") {
        CrashByAccess();
    } else if (mode == "pure") {
        CrashByPureCall();
    } else if (mode == "exception") {
        CrashByException();
    } else if (mode == "abort") {
        std::abort();
    } else if (mode == "overflow") {
        return CrashByOverflow(0);
    } else if (mode == "assert") {
        // As SPOT_ASSERT() fails, in every configuration.
        Spot::Detail::AssertionFailed("s_zero == 1", std::source_location::current(), "the test asked for it");
    }
    return 0;
}
