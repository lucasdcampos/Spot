#include "spot/core/crash_handler_internal.h"

#include <array>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>

#include <cxxabi.h>
#include <dlfcn.h>
#include <signal.h>
#include <unistd.h>

#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define SPOT_HAS_BACKTRACE 1
#endif

namespace Spot::Detail {

namespace {

constexpr std::array CrashSignals{SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};

std::array<struct sigaction, CrashSignals.size()> s_previousActions;
// The handler runs on a stack of its own, as the crashed one may have overflowed.
alignas(16) char s_alternateStack[256 * 1024];

std::string Demangle(const char* name) {
    int status = 0;
    char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string result = status == 0 && demangled ? demangled : name;
    std::free(demangled);
    return result;
}

// "module+0x1234 Function+0x12", with what is known of it.
std::string DescribeAddress(void* address) {
    Dl_info info = {};
    if (!dladdr(address, &info) || !info.dli_fname) {
        return std::format("{}", address);
    }
    std::string module = info.dli_fname;
    if (const std::size_t slash = module.rfind('/'); slash != std::string::npos) {
        module = module.substr(slash + 1);
    }
    const auto offset = reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(info.dli_fbase);
    std::string text = std::format("{}+0x{:x}", module, offset);
    if (info.dli_sname) {
        text +=
            std::format(" {}+0x{:x}", Demangle(info.dli_sname),
                        reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(info.dli_saddr));
    }
    return text;
}

const char* DescribeSignal(int signal) {
    switch (signal) {
    case SIGSEGV:
        return "segmentation fault: a bad memory access";
    case SIGBUS:
        return "bus error: a bad memory access";
    case SIGILL:
        return "illegal instruction";
    case SIGFPE:
        return "arithmetic error, such as an integer division by zero";
    case SIGABRT:
        return "the program aborted";
    default:
        return "a fatal signal";
    }
}

void OnSignal(int signal, siginfo_t* info, void* /*context*/) {
    if (BeginCrash()) {
        std::string what = DescribeSignal(signal);
        if (signal != SIGABRT && info) {
            what += std::format(", at address {}", info->si_addr);
        }
        const std::filesystem::path report = ReportCrash(what, CaptureStack());
        ShowCrashMessage(what, report);
    }
    // Ends the process as it would have ended, through the handler that was there before.
    for (std::size_t index = 0; index < CrashSignals.size(); ++index) {
        if (CrashSignals[index] == signal) {
            sigaction(signal, &s_previousActions[index], nullptr);
        }
    }
    raise(signal);
}

} // namespace

void InstallPlatformCrashHandlers() {
    stack_t stack = {};
    stack.ss_sp = s_alternateStack;
    stack.ss_size = sizeof(s_alternateStack);
    sigaltstack(&stack, nullptr);

    struct sigaction action = {};
    action.sa_sigaction = OnSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (std::size_t index = 0; index < CrashSignals.size(); ++index) {
        sigaction(CrashSignals[index], &action, &s_previousActions[index]);
    }
}

std::string CaptureStack() {
#ifdef SPOT_HAS_BACKTRACE
    constexpr int MaxFrames = 64;
    void* frames[MaxFrames];
    const int count = backtrace(frames, MaxFrames);
    std::string stack;
    for (int index = 0; index < count; ++index) {
        stack += std::format("  #{} {}\n", index, DescribeAddress(frames[index]));
    }
    return stack.empty() ? "  (unknown)\n" : stack;
#else
    return "  (unknown: the platform has no backtrace())\n";
#endif
}

void ShowCrashMessage(std::string_view what, const std::filesystem::path& report) {
    // Without a message box of the system's own, standard error tells whoever started the program.
    const CrashHandlerConfig& config = GetCrashHandlerConfig();
    const std::string text = std::format(
        "{} crashed: {}. {}\n", config.applicationName.empty() ? "The program" : config.applicationName, what,
        report.empty() ? "The log has the details."
                       : "The log has the details, and the report is in " + report.string());
    std::fwrite(text.data(), 1, text.size(), stderr);
}

} // namespace Spot::Detail
