#include "spot/platform/frame_capture.h"

#include "spot/core/io/paths.h"
#include "spot/platform/process.h"

#include <SDL3/SDL_loadso.h>
#include <renderdoc_app.h>

#include <bit>
#include <cstdlib>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Spot {

struct FrameCapture::Impl {
    SDL_SharedObject* library = nullptr;
    RENDERDOC_API_1_6_0* api = nullptr;
    ~Impl() {
        // RenderDoc's hooks and resources can outlive the application/device; keep connected runtimes loaded.
        if (library && !api) {
            SDL_UnloadObject(library);
        }
    }
};

FrameCapture::FrameCapture(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}
FrameCapture::~FrameCapture() = default;

std::unique_ptr<FrameCapture> FrameCapture::Connect(const std::filesystem::path& libraryPath) {
#if defined(__APPLE__)
    (void)libraryPath;
    return nullptr;
#else
    auto impl = std::make_unique<Impl>();
    pRENDERDOC_GetAPI getApi = nullptr;
#if defined(_WIN32)
    if (const auto injected = libraryPath.empty() ? GetModuleHandleW(L"renderdoc.dll") : nullptr) {
        getApi = std::bit_cast<pRENDERDOC_GetAPI>(GetProcAddress(injected, "RENDERDOC_GetAPI"));
    }
#endif
    if (!getApi) {
        std::filesystem::path path = libraryPath;
        if (path.empty()) {
            if (const auto overridePath = Process::GetEnvironmentValue("SPOT_RENDERDOC_LIBRARY")) {
                path = Utf8ToPath(*overridePath);
            }
        }
        if (path.empty()) {
#if defined(_WIN32)
            if (const auto programFiles = Process::GetEnvironmentValue("ProgramFiles")) {
                path = Utf8ToPath(*programFiles) / "RenderDoc" / "renderdoc.dll";
            }
            std::error_code error;
            if (path.empty() || !std::filesystem::is_regular_file(path, error)) {
                return nullptr;
            }
#else
            path = "librenderdoc.so";
#endif
        }
        impl->library = SDL_LoadObject(PathToUtf8(path).c_str());
        if (!impl->library) {
            return nullptr;
        }
        getApi = std::bit_cast<pRENDERDOC_GetAPI>(SDL_LoadFunction(impl->library, "RENDERDOC_GetAPI"));
    }
    void* api = nullptr;
    if (!getApi || getApi(eRENDERDOC_API_Version_1_6_0, &api) != 1 || !api) {
        return nullptr;
    }
    impl->api = static_cast<RENDERDOC_API_1_6_0*>(api);
    impl->api->SetCaptureKeys(nullptr, 0);
    impl->api->MaskOverlayBits(0, 0);
    return std::unique_ptr<FrameCapture>(new FrameCapture(std::move(impl)));
#endif
}

bool FrameCapture::Request(const std::filesystem::path& fileTemplate) {
    std::error_code error;
    if (!fileTemplate.parent_path().empty()) {
        std::filesystem::create_directories(fileTemplate.parent_path(), error);
    }
    if (error || fileTemplate.empty()) {
        return false;
    }
    m_impl->api->SetCaptureFilePathTemplate(PathToUtf8(fileTemplate).c_str());
    m_impl->api->TriggerCapture();
    return true;
}

} // namespace Spot
