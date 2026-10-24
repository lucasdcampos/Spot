#include "spot/physics/jolt/jolt_runtime.h"

#include "spot/core/debug/log.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace Spot::Detail {

namespace {

std::mutex s_mutex;
int s_users = 0;
JoltRuntime* s_runtime = nullptr;

void TraceToLog(const char* format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Log::GetEngineLogger().Info("Jolt: {}", message);
}

#ifdef JPH_ENABLE_ASSERTS
// Without this, a failed Jolt assert stops the program at a breakpoint without saying why.
bool AssertFailedToLog(const char* expression, const char* message, const char* file, JPH::uint line) {
    Log::GetEngineLogger().Error("Jolt assert failed at {}:{}: {}{}{}", file, line, expression, message ? ": " : "",
                                 message ? message : "");
    Log::Flush();
    return true;
}
#endif

} // namespace

JoltRuntimeUser::JoltRuntimeUser() {
    const std::scoped_lock lock(s_mutex);
    if (s_users++ == 0) {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = &TraceToLog;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = &AssertFailedToLog;)
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        s_runtime = new JoltRuntime();
    }
}

JoltRuntimeUser::~JoltRuntimeUser() {
    const std::scoped_lock lock(s_mutex);
    if (--s_users == 0) {
        delete s_runtime;
        s_runtime = nullptr;
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

JoltRuntime& JoltRuntimeUser::Get() const {
    return *s_runtime;
}

} // namespace Spot::Detail
