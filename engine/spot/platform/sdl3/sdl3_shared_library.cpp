#include "spot/core/shared_library.h"

#include "spot/core/log.h"
#include "spot/core/paths.h"

#include <SDL3/SDL_loadso.h>

namespace Spot {

std::unique_ptr<SharedLibrary> SharedLibrary::Load(const std::filesystem::path& path) {
    SDL_SharedObject* handle = SDL_LoadObject(PathToUtf8(path).c_str());
    if (!handle) {
        Log::GetEngineLogger().Error("Could not load the library '{}': {}", PathToUtf8(path), SDL_GetError());
        return nullptr;
    }
    return std::unique_ptr<SharedLibrary>(new SharedLibrary(handle, path));
}

SharedLibrary::~SharedLibrary() {
    SDL_UnloadObject(static_cast<SDL_SharedObject*>(m_handle));
}

void* SharedLibrary::GetFunction(const char* name) const {
    // Function and object pointers have the same size on every platform SDL supports.
    return reinterpret_cast<void*>(SDL_LoadFunction(static_cast<SDL_SharedObject*>(m_handle), name));
}

} // namespace Spot
