#include "spot/platform/file_dialog.h"

#include "spot/core/log.h"
#include "spot/core/paths.h"
#include "spot/platform/platform.h"
#include "spot/platform/window.h"

#include <SDL3/SDL.h>

#include <memory>
#include <utility>

namespace Spot::FileDialog {

namespace {

// Lives from the dialog's opening to its callback, since SDL reads the filters until then.
struct Request {
    std::vector<std::string> names;
    std::vector<std::string> patterns;
    std::vector<SDL_DialogFileFilter> filters;
    std::string location;
    Callback callback;
    std::vector<std::filesystem::path> paths;
    bool platformInitialized = false;
};

void SDLCALL Deliver(void* userdata) {
    std::unique_ptr<Request> request(static_cast<Request*>(userdata));
    if (request->callback) {
        request->callback(std::move(request->paths));
    }
    if (request->platformInitialized) {
        Platform::Shutdown();
    }
}

void SDLCALL OnChosen(void* userdata, const char* const* files, int) {
    auto* request = static_cast<Request*>(userdata);
    if (!files) {
        Log::GetEngineLogger().Error("The file dialog failed: {}", SDL_GetError());
    } else {
        for (const char* const* file = files; *file; ++file) {
            request->paths.push_back(Utf8ToPath(*file));
        }
    }
    // Dialogs may answer from another thread; callbacks run on the main one, like the rest of the application.
    if (!SDL_RunOnMainThread(Deliver, request, false)) {
        Log::GetEngineLogger().Error("The file dialog's answer was lost: {}", SDL_GetError());
        delete request;
    }
}

std::unique_ptr<Request> MakeRequest(std::span<const FileFilter> filters, Callback callback,
                                     const std::filesystem::path& location) {
    auto request = std::make_unique<Request>();
    request->platformInitialized = Platform::Initialize();
    request->callback = std::move(callback);
    request->location = location.empty() ? std::string() : PathToUtf8(location);
    request->names.reserve(filters.size());
    request->patterns.reserve(filters.size());
    for (const FileFilter& filter : filters) {
        request->names.push_back(filter.name);
        request->patterns.push_back(filter.extensions);
    }
    for (std::size_t i = 0; i < filters.size(); ++i) {
        request->filters.push_back({request->names[i].c_str(), request->patterns[i].c_str()});
    }
    return request;
}

SDL_Window* GetNative(Window* window) {
    return window ? static_cast<SDL_Window*>(window->GetNativeHandle()) : nullptr;
}

} // namespace

void OpenFile(Window* window, std::span<const FileFilter> filters, Callback callback, bool allowMany,
              const std::filesystem::path& location) {
    auto request = MakeRequest(filters, std::move(callback), location);
    Request& raw = *request;
    SDL_ShowOpenFileDialog(OnChosen, request.release(), GetNative(window), raw.filters.data(),
                           static_cast<int>(raw.filters.size()), raw.location.empty() ? nullptr : raw.location.c_str(),
                           allowMany);
}

void SaveFile(Window* window, std::span<const FileFilter> filters, Callback callback,
              const std::filesystem::path& location) {
    auto request = MakeRequest(filters, std::move(callback), location);
    Request& raw = *request;
    SDL_ShowSaveFileDialog(OnChosen, request.release(), GetNative(window), raw.filters.data(),
                           static_cast<int>(raw.filters.size()), raw.location.empty() ? nullptr : raw.location.c_str());
}

void OpenFolder(Window* window, Callback callback, const std::filesystem::path& location) {
    auto request = MakeRequest({}, std::move(callback), location);
    Request& raw = *request;
    SDL_ShowOpenFolderDialog(OnChosen, request.release(), GetNative(window),
                             raw.location.empty() ? nullptr : raw.location.c_str(), false);
}

} // namespace Spot::FileDialog
