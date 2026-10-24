#include "spot/platform/shell.h"

#include "spot/core/debug/log.h"
#include "spot/core/io/paths.h"
#include "spot/platform/platform.h"

#include <SDL3/SDL.h>

#include <string>

namespace Spot::Shell {

bool Open(const std::filesystem::path& path) {
    return OpenUrl(ToFileUrl(std::filesystem::absolute(path)));
}

bool OpenUrl(std::string_view url) {
    if (!Platform::Initialize()) {
        return false;
    }
    // SDL needs a null-terminated string.
    const std::string copy(url);
    const bool opened = SDL_OpenURL(copy.c_str());
    if (!opened) {
        Log::GetEngineLogger().Error("Could not open '{}': {}", copy, SDL_GetError());
    }
    Platform::Shutdown();
    return opened;
}

} // namespace Spot::Shell
