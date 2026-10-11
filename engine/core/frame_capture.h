#pragma once

#include <filesystem>
#include <memory>

namespace Spot {

// Optional RenderDoc integration. Connect before creating graphics; absence is a normal, silent result.
class FrameCapture {
public:
    static std::unique_ptr<FrameCapture> Connect(const std::filesystem::path& libraryPath = {});
    ~FrameCapture();
    FrameCapture(const FrameCapture&) = delete;
    FrameCapture& operator=(const FrameCapture&) = delete;
    // Requests the next presented frame. Files use this template; no external UI is launched.
    bool Request(const std::filesystem::path& fileTemplate);

private:
    struct Impl;
    explicit FrameCapture(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
