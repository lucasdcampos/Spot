#pragma once

#include "math/vector.h"

#include <memory>
#include <string>
#include <string_view>

namespace Spot {

struct WindowConfig {
    std::string title = "Spot";
    int width = 1280;
    int height = 720;
    bool resizable = true;
};

class Window {
public:
    // Returns null, after logging why, when the window cannot be created.
    static std::unique_ptr<Window> Create(const WindowConfig& config = {});

    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Sizes are in screen coordinates, which differ from pixels on high-DPI displays.
    int GetWidth() const;
    int GetHeight() const;
    Vector2 GetSize() const;
    // The size of the drawable area in pixels, which is what rendering uses.
    int GetPixelWidth() const;
    int GetPixelHeight() const;

    const std::string& GetTitle() const;
    void SetTitle(std::string_view title);

    // A captured mouse is hidden and kept inside the window, and still reports how far it moves, as first-person
    // cameras need. Returns false, after logging why, when the platform cannot capture it.
    bool IsMouseCaptured() const;
    bool SetMouseCaptured(bool captured);

    // The backend's window (an SDL_Window* with SDL3), for code that needs the backend directly.
    void* GetNativeHandle() const;

private:
    // Defined by the platform backend, which keeps its types out of this header.
    struct Impl;

    explicit Window(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
