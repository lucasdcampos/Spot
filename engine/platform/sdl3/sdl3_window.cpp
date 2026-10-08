#include "window/window.h"

#include "core/log.h"
#include "platform/platform.h"
#include "platform/sdl3/sdl3_mouse_capture.h"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <utility>

namespace Spot {

namespace {

constexpr std::size_t CursorCount = static_cast<std::size_t>(Cursor::Crosshair) + 1;

SDL_SystemCursor ToSdl(Cursor cursor) {
    switch (cursor) {
    case Cursor::Arrow:
        return SDL_SYSTEM_CURSOR_DEFAULT;
    case Cursor::Text:
        return SDL_SYSTEM_CURSOR_TEXT;
    case Cursor::Hand:
        return SDL_SYSTEM_CURSOR_POINTER;
    case Cursor::Move:
        return SDL_SYSTEM_CURSOR_MOVE;
    case Cursor::ResizeNs:
        return SDL_SYSTEM_CURSOR_NS_RESIZE;
    case Cursor::ResizeEw:
        return SDL_SYSTEM_CURSOR_EW_RESIZE;
    case Cursor::ResizeNesw:
        return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    case Cursor::ResizeNwse:
        return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    case Cursor::NotAllowed:
        return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    case Cursor::Wait:
        return SDL_SYSTEM_CURSOR_WAIT;
    case Cursor::Progress:
        return SDL_SYSTEM_CURSOR_PROGRESS;
    case Cursor::Crosshair:
        return SDL_SYSTEM_CURSOR_CROSSHAIR;
    }
    return SDL_SYSTEM_CURSOR_DEFAULT;
}

} // namespace

struct Window::Impl {
    Impl(SDL_Window* handle, std::string title) : handle(handle), title(std::move(title)) {}

    ~Impl() {
        for (SDL_Cursor* created : cursors) {
            SDL_DestroyCursor(created);
        }
        SDL_DestroyWindow(handle);
        Platform::Shutdown();
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    SDL_Window* handle;
    std::string title;
    // Created on first use, since most applications only ever show the arrow.
    std::array<SDL_Cursor*, CursorCount> cursors{};
    Cursor cursor = Cursor::Arrow;
};

std::unique_ptr<Window> Window::Create(const WindowConfig& config) {
    if (!Platform::Initialize()) {
        return nullptr;
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (config.maximized) {
        flags |= SDL_WINDOW_MAXIMIZED;
    }
    SDL_Window* handle = SDL_CreateWindow(config.title.c_str(), config.width, config.height, flags);
    if (!handle) {
        Log::GetEngineLogger().Error("Failed to create window: {}", SDL_GetError());
        Platform::Shutdown();
        return nullptr;
    }

    return std::unique_ptr<Window>(new Window(std::make_unique<Impl>(handle, config.title)));
}

Window::Window(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

Window::~Window() = default;

int Window::GetWidth() const {
    int width = 0;
    SDL_GetWindowSize(m_impl->handle, &width, nullptr);
    return width;
}

int Window::GetHeight() const {
    int height = 0;
    SDL_GetWindowSize(m_impl->handle, nullptr, &height);
    return height;
}

Vector2 Window::GetSize() const {
    return {static_cast<float>(GetWidth()), static_cast<float>(GetHeight())};
}

void Window::SetSize(int width, int height) {
    if (!SDL_SetWindowSize(m_impl->handle, width, height)) {
        Log::GetEngineLogger().Error("Failed to resize the window: {}", SDL_GetError());
    }
}

int Window::GetPixelWidth() const {
    int width = 0;
    SDL_GetWindowSizeInPixels(m_impl->handle, &width, nullptr);
    return width;
}

int Window::GetPixelHeight() const {
    int height = 0;
    SDL_GetWindowSizeInPixels(m_impl->handle, nullptr, &height);
    return height;
}

float Window::GetContentScale() const {
    // SDL's display scale also counts the pixels per screen coordinate, which the pixel size already covers.
    const float displayScale = SDL_GetWindowDisplayScale(m_impl->handle);
    const float pixelDensity = SDL_GetWindowPixelDensity(m_impl->handle);
    if (displayScale <= 0.0f || pixelDensity <= 0.0f || !std::isfinite(displayScale / pixelDensity)) {
        return 1.0f;
    }
    return displayScale / pixelDensity;
}

bool Window::IsMaximized() const {
    return (SDL_GetWindowFlags(m_impl->handle) & SDL_WINDOW_MAXIMIZED) != 0;
}

bool Window::IsMinimized() const {
    return (SDL_GetWindowFlags(m_impl->handle) & SDL_WINDOW_MINIMIZED) != 0;
}

void Window::Maximize() {
    SDL_MaximizeWindow(m_impl->handle);
}

void Window::Restore() {
    SDL_RestoreWindow(m_impl->handle);
}

const std::string& Window::GetTitle() const {
    return m_impl->title;
}

void Window::SetTitle(std::string_view title) {
    m_impl->title = title;
    SDL_SetWindowTitle(m_impl->handle, m_impl->title.c_str());
}

bool Window::IsMouseCaptured() const {
    return SDL_GetWindowRelativeMouseMode(m_impl->handle);
}

bool Window::SetMouseCaptured(bool captured) {
    if (captured == IsMouseCaptured()) {
        return true;
    }
    const SDL_PropertiesID properties = SDL_GetWindowProperties(m_impl->handle);
    if (captured) {
        float x = 0.0f;
        float y = 0.0f;
        SDL_GetMouseState(&x, &y);
        SDL_SetFloatProperty(properties, Platform::CapturedMouseXProperty, x);
        SDL_SetFloatProperty(properties, Platform::CapturedMouseYProperty, y);
    }
    if (!SDL_SetWindowRelativeMouseMode(m_impl->handle, captured)) {
        Log::GetEngineLogger().Error("Failed to {} the mouse: {}", captured ? "capture" : "release", SDL_GetError());
        return false;
    }
    if (!captured) {
        SDL_WarpMouseInWindow(m_impl->handle, SDL_GetFloatProperty(properties, Platform::CapturedMouseXProperty, 0.0f),
                              SDL_GetFloatProperty(properties, Platform::CapturedMouseYProperty, 0.0f));
    }
    return true;
}

Cursor Window::GetCursor() const {
    return m_impl->cursor;
}

void Window::SetCursor(Cursor cursor) {
    m_impl->cursor = cursor;
    SDL_Cursor*& native = m_impl->cursors[static_cast<std::size_t>(cursor)];
    if (!native) {
        native = SDL_CreateSystemCursor(ToSdl(cursor));
    }
    // Platforms without system cursors keep whatever they show.
    if (native) {
        SDL_SetCursor(native);
    }
}

bool Window::IsCursorVisible() const {
    return SDL_CursorVisible();
}

void Window::SetCursorVisible(bool visible) {
    if (visible) {
        SDL_ShowCursor();
    } else {
        SDL_HideCursor();
    }
}

bool Window::IsTextInputActive() const {
    return SDL_TextInputActive(m_impl->handle);
}

void Window::StartTextInput() {
    if (!SDL_StartTextInput(m_impl->handle)) {
        Log::GetEngineLogger().Error("Failed to start text input: {}", SDL_GetError());
    }
}

void Window::StopTextInput() {
    SDL_StopTextInput(m_impl->handle);
}

void Window::SetTextInputArea(Vector2 position, Vector2 size) {
    const SDL_Rect area{static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(size.x),
                        static_cast<int>(size.y)};
    SDL_SetTextInputArea(m_impl->handle, &area, 0);
}

void* Window::GetNativeHandle() const {
    return m_impl->handle;
}

} // namespace Spot
