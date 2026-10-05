#include "window/window.h"

#include "core/log.h"
#include "platform/platform.h"

#include <SDL3/SDL.h>

#include <utility>

namespace Spot {

struct Window::Impl {
    Impl(SDL_Window* handle, std::string title) : handle(handle), title(std::move(title)) {}

    ~Impl() {
        SDL_DestroyWindow(handle);
        Platform::Shutdown();
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    SDL_Window* handle;
    std::string title;
};

std::unique_ptr<Window> Window::Create(const WindowConfig& config) {
    if (!Platform::Initialize()) {
        return nullptr;
    }

    const SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | (config.resizable ? SDL_WINDOW_RESIZABLE : 0);
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
    if (!SDL_SetWindowRelativeMouseMode(m_impl->handle, captured)) {
        Log::GetEngineLogger().Error("Failed to {} the mouse: {}", captured ? "capture" : "release", SDL_GetError());
        return false;
    }
    return true;
}

void* Window::GetNativeHandle() const {
    return m_impl->handle;
}

} // namespace Spot
