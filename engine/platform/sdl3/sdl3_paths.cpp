#include "core/paths.h"

#include "core/log.h"

#include <SDL3/SDL.h>

#include <string>
#include <string_view>

namespace Spot {

std::filesystem::path GetExecutableDirectory() {
    const char* basePath = SDL_GetBasePath();
    if (!basePath) {
        Log::GetEngineLogger().Error("Could not find the executable directory: {}", SDL_GetError());
        return std::filesystem::current_path();
    }
    return Utf8ToPath(basePath);
}

std::filesystem::path GetUserDataDirectory(std::string_view organization, std::string_view application) {
    // SDL needs null-terminated strings.
    const std::string org(organization);
    const std::string app(application);
    char* path = SDL_GetPrefPath(org.c_str(), app.c_str());
    if (!path) {
        Log::GetEngineLogger().Error("Could not find a user data directory: {}", SDL_GetError());
        return {};
    }
    std::filesystem::path result = Utf8ToPath(path);
    SDL_free(path);
    // SDL ends the path with a separator, which would make it look like it has no name.
    return result.has_filename() ? result : result.parent_path();
}

} // namespace Spot
