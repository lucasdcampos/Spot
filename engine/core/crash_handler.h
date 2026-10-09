#pragma once

#include <filesystem>
#include <string>

namespace Spot {

struct CrashHandlerConfig {
    // Names the program in reports and the message.
    std::string applicationName;
    // Where each crash leaves a report: a text file with what happened and the stack, and on Windows a minidump that
    // debuggers open. None are written when empty.
    std::filesystem::path reportDirectory;
    // Tells the user that the program crashed and where the report is, in a message box where the platform has them,
    // as tools do. Programs that run unattended, such as tests, leave it off.
    bool showMessage = false;
};

// Makes crashes leave a trace: when the process is about to die of a bad memory access, a call to a pure virtual
// function, an uncaught exception, an abort or a stack overflow, the crash and the stack that led to it are logged to
// every log sink, which are flushed, and a report is written, before the process ends as it would have. Debuggers
// attached to the process still stop at the crash first.
//
// Applications install it with their configuration (see ApplicationConfig); installing it again replaces the
// configuration.
void InstallCrashHandler(const CrashHandlerConfig& config);

} // namespace Spot
