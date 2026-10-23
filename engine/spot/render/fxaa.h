#pragma once

#include "spot/graphics/texture.h"
#include "spot/render/fullscreen_pass.h"

#include <memory>

namespace Spot {

class GraphicsDevice;
class RenderPass;

// Fast approximate antialiasing: smooths the steps of edges in a finished image, those of thin highlights and
// alpha-tested shapes too, which multisampling does not reach, for little cost; it softens the image a little. It
// reads the image after tonemapping. Built on FullscreenPass.
class Fxaa {
public:
    // The format of the targets it draws into. Null, after logging why, when its resources cannot be created.
    static std::unique_ptr<Fxaa> Create(GraphicsDevice& device, TextureFormat targetFormat);

    // Draws the antialiased image into the pass's target, which it covers and which must be its size.
    void Draw(RenderPass& pass, const Texture& image) const;

private:
    Fxaa() = default;

    std::unique_ptr<FullscreenPass> m_pass;
    std::unique_ptr<Sampler> m_sampler;
};

} // namespace Spot
