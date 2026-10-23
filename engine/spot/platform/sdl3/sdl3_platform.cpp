#include "spot/platform/platform.h"

#include "spot/core/assert.h"
#include "spot/core/log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>

namespace Spot::Platform {

namespace {

int s_initCount = 0;

Logger& GetSdlLogger() {
    static Logger logger("SDL");
    return logger;
}

LogLevel ToLogLevel(SDL_LogPriority priority) {
    switch (priority) {
    case SDL_LOG_PRIORITY_TRACE:
    case SDL_LOG_PRIORITY_VERBOSE:
        return LogLevel::Trace;
    case SDL_LOG_PRIORITY_DEBUG:
        return LogLevel::Debug;
    case SDL_LOG_PRIORITY_INFO:
        return LogLevel::Info;
    case SDL_LOG_PRIORITY_WARN:
        return LogLevel::Warning;
    default:
        return LogLevel::Error;
    }
}

void SDLCALL WriteSdlLog(void*, int, SDL_LogPriority priority, const char* message) {
    GetSdlLogger().WriteMessage(ToLogLevel(priority), message);
}

} // namespace

bool Initialize() {
    if (s_initCount == 0) {
        SDL_SetLogOutputFunction(WriteSdlLog, nullptr);
        // Closing the window already reports a close request; without this SDL would also send a quit event.
        SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");

        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            Log::GetEngineLogger().Error("Failed to initialize SDL: {}", SDL_GetError());
            return false;
        }

        // Without gamepads, the rest still works.
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
            Log::GetEngineLogger().Warning("Gamepads are unavailable: {}", SDL_GetError());
        }

        const int version = SDL_GetVersion();
        Log::GetEngineLogger().Debug("SDL {}.{}.{} initialized with the '{}' video driver",
                                     SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
                                     SDL_VERSIONNUM_MICRO(version), SDL_GetCurrentVideoDriver());
    }

    ++s_initCount;
    return true;
}

void SleepFor(std::chrono::nanoseconds duration) {
    if (duration > std::chrono::nanoseconds::zero()) {
        SDL_DelayPrecise(static_cast<Uint64>(duration.count()));
    }
}

bool RumbleGamepad(GamepadId gamepad, float low, float high, std::chrono::milliseconds duration) {
    SDL_Gamepad* handle = SDL_GetGamepadFromID(static_cast<SDL_JoystickID>(gamepad));
    const auto strength = [](float value) { return static_cast<Uint16>(std::clamp(value, 0.0f, 1.0f) * 65535.0f); };
    return handle && SDL_RumbleGamepad(handle, strength(low), strength(high),
                                       static_cast<Uint32>(std::max<std::int64_t>(duration.count(), 0)));
}

void Shutdown() {
    SPOT_ASSERT(s_initCount > 0, "Platform::Shutdown() called without a matching Initialize()");

    if (--s_initCount == 0) {
        SDL_Quit();
    }
}

} // namespace Spot::Platform
