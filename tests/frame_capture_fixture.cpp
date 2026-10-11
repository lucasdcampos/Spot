#include <renderdoc_app.h>

#include <string>

#if defined(_WIN32)
#define SPOT_CAPTURE_EXPORT extern "C" __declspec(dllexport)
#else
#define SPOT_CAPTURE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
unsigned s_count = 0;
std::string s_path;
void RENDERDOC_CC SetPath(const char* path) {
    s_path = path;
}
void RENDERDOC_CC Trigger() {
    ++s_count;
}
void RENDERDOC_CC SetKeys(RENDERDOC_InputButton*, int) {
}
void RENDERDOC_CC Mask(unsigned, unsigned) {
}
} // namespace

SPOT_CAPTURE_EXPORT int RENDERDOC_CC RENDERDOC_GetAPI(RENDERDOC_Version version, void** output) {
    static RENDERDOC_API_1_6_0 api{};
    if (version != eRENDERDOC_API_Version_1_6_0 || !output) {
        return 0;
    }
    api.SetCaptureFilePathTemplate = SetPath;
    api.TriggerCapture = Trigger;
    api.SetCaptureKeys = SetKeys;
    api.MaskOverlayBits = Mask;
    *output = &api;
    return 1;
}

SPOT_CAPTURE_EXPORT unsigned SpotGetCaptureCount() {
    return s_count;
}
SPOT_CAPTURE_EXPORT const char* SpotGetCaptureTemplate() {
    return s_path.c_str();
}
