#include "spot/platform/clipboard.h"

#include "spot/core/debug/log.h"
#include "spot/platform/platform.h"

#include <SDL3/SDL.h>

namespace Spot::Clipboard {

namespace {

// The clipboard belongs to the video subsystem, which must run while it is used, even without a window.
class ScopedPlatform {
public:
    ScopedPlatform() : m_initialized(Platform::Initialize()) {}

    ~ScopedPlatform() {
        if (m_initialized) {
            Platform::Shutdown();
        }
    }

    ScopedPlatform(const ScopedPlatform&) = delete;
    ScopedPlatform& operator=(const ScopedPlatform&) = delete;

    explicit operator bool() const { return m_initialized; }

private:
    bool m_initialized;
};

} // namespace

bool HasText() {
    const ScopedPlatform platform;
    return platform && SDL_HasClipboardText();
}

std::string GetText() {
    const ScopedPlatform platform;
    if (!platform) {
        return {};
    }
    char* text = SDL_GetClipboardText();
    std::string result = text ? text : "";
    SDL_free(text);
    return result;
}

bool SetText(std::string_view text) {
    const ScopedPlatform platform;
    if (!platform) {
        return false;
    }
    // SDL needs a null-terminated string.
    const std::string copy(text);
    if (!SDL_SetClipboardText(copy.c_str())) {
        Log::GetEngineLogger().Error("Failed to set the clipboard text: {}", SDL_GetError());
        return false;
    }
    return true;
}

} // namespace Spot::Clipboard
