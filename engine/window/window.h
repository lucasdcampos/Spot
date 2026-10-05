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
    // Opens the window maximized; its size is then the one it gets back when restored.
    bool maximized = false;
};

// Shapes of the mouse cursor, as the OS draws them.
enum class Cursor {
    Arrow,
    Text,
    Hand,
    Move,
    ResizeNs,
    ResizeEw,
    ResizeNesw,
    ResizeNwse,
    NotAllowed,
    Wait,
    Progress,
    Crosshair,
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
    void SetSize(int width, int height);
    // The size of the drawable area in pixels, which is what rendering uses.
    int GetPixelWidth() const;
    int GetPixelHeight() const;
    // How much larger than screen coordinates the OS asks content to be drawn, such as 1.5 at 150% scaling on
    // Windows. Where screen coordinates are already scaled, as points are on macOS, it is usually 1.
    float GetContentScale() const;

    bool IsMaximized() const;
    bool IsMinimized() const;
    void Maximize();
    // Brings a maximized or minimized window back to its normal size.
    void Restore();

    const std::string& GetTitle() const;
    void SetTitle(std::string_view title);

    // A captured mouse is hidden and kept inside the window, and still reports how far it moves, as first-person
    // cameras need. Returns false, after logging why, when the platform cannot capture it.
    bool IsMouseCaptured() const;
    bool SetMouseCaptured(bool captured);

    // Platforms without a cursor ignore these.
    Cursor GetCursor() const;
    void SetCursor(Cursor cursor);
    bool IsCursorVisible() const;
    void SetCursorVisible(bool visible);

    // While text input is on, typed text arrives as TextInputEvents. Turn it on only while a text field has focus:
    // some platforms show an on-screen keyboard or an input method window meanwhile.
    bool IsTextInputActive() const;
    void StartTextInput();
    void StopTextInput();
    // Where the text being typed is, in screen coordinates, so input method windows open next to it.
    void SetTextInputArea(Vector2 position, Vector2 size);

    // The backend's window (an SDL_Window* with SDL3), for code that needs the backend directly.
    void* GetNativeHandle() const;

private:
    // Defined by the platform backend, which keeps its types out of this header.
    struct Impl;

    explicit Window(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace Spot
