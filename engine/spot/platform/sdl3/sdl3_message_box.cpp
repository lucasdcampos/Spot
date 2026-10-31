#include "spot/platform/message_box.h"

#include "spot/core/debug/log.h"
#include "spot/platform/platform.h"
#include "spot/platform/window.h"

#include <SDL3/SDL.h>

#include <string>
#include <utility>

namespace Spot::MessageBox {

namespace {

Handler& GetHandler() {
    static Handler s_handler;
    return s_handler;
}

SDL_MessageBoxFlags ToSdlFlags(MessageBoxIcon icon) {
    switch (icon) {
    case MessageBoxIcon::Information:
        return SDL_MESSAGEBOX_INFORMATION;
    case MessageBoxIcon::Warning:
        return SDL_MESSAGEBOX_WARNING;
    case MessageBoxIcon::Error:
        return SDL_MESSAGEBOX_ERROR;
    }
    return SDL_MESSAGEBOX_INFORMATION;
}

} // namespace

void SetHandler(Handler handler) {
    GetHandler() = std::move(handler);
}

bool Show(std::string_view title, std::string_view message, MessageBoxIcon icon, Window* window) {
    if (const Handler& handler = GetHandler()) {
        return handler(title, message, icon, window);
    }
    const bool initialized = Platform::Initialize();
    if (const char* driver = SDL_GetCurrentVideoDriver(); driver && std::string_view(driver) == "dummy") {
        if (initialized) {
            Platform::Shutdown();
        }
        return true;
    }
    const std::string titleCopy(title);
    const std::string messageCopy(message);
    SDL_Window* native = window ? static_cast<SDL_Window*>(window->GetNativeHandle()) : nullptr;
    const bool wasCaptured = window && window->IsMouseCaptured();
    if (wasCaptured) {
        window->SetMouseCaptured(false);
    }
    const bool shown = SDL_ShowSimpleMessageBox(ToSdlFlags(icon), titleCopy.c_str(), messageCopy.c_str(), native);
    if (!shown) {
        Log::GetEngineLogger().Error("Could not show message box '{}': {}", titleCopy, SDL_GetError());
    }
    if (wasCaptured) {
        window->SetMouseCaptured(true);
    }
    if (initialized) {
        Platform::Shutdown();
    }
    return shown;
}

} // namespace Spot::MessageBox
