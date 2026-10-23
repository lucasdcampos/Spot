#include "spot/core/paths.h"

#include "spot/core/log.h"

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

std::filesystem::path GetUserFolder(UserFolder folder) {
    SDL_Folder native = SDL_FOLDER_HOME;
    switch (folder) {
    case UserFolder::Home:
        native = SDL_FOLDER_HOME;
        break;
    case UserFolder::Desktop:
        native = SDL_FOLDER_DESKTOP;
        break;
    case UserFolder::Documents:
        native = SDL_FOLDER_DOCUMENTS;
        break;
    case UserFolder::Downloads:
        native = SDL_FOLDER_DOWNLOADS;
        break;
    }
    const char* path = SDL_GetUserFolder(native);
    if (!path) {
        Log::GetEngineLogger().Error("Could not find a user folder: {}", SDL_GetError());
        return {};
    }
    const std::filesystem::path result = Utf8ToPath(path);
    return result.has_filename() ? result : result.parent_path();
}

} // namespace Spot
