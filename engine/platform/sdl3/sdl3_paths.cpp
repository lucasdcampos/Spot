#include "core/paths.h"

#include "core/log.h"

#include <SDL3/SDL.h>

#include <string_view>

namespace Spot {

std::filesystem::path GetExecutableDirectory() {
    const char* basePath = SDL_GetBasePath();
    if (!basePath) {
        Log::GetEngineLogger().Error("Could not find the executable directory: {}", SDL_GetError());
        return std::filesystem::current_path();
    }
    // SDL returns UTF-8, which std::filesystem only reads as such from char8_t.
    const std::string_view utf8(basePath);
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

} // namespace Spot
