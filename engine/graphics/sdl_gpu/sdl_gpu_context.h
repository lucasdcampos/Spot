#pragma once

// Backend-internal definition of the render context, shared by the SDL_GPU sources.

#include "graphics/render_context.h"

#include <SDL3/SDL.h>

namespace Spot {

struct RenderContext::Impl {
    SDL_GPUCommandBuffer* commandBuffer = nullptr;
    SDL_GPUTexture* target = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    bool passOpen = false;
};

} // namespace Spot
