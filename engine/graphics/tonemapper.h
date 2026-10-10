#pragma once

#include "graphics/fullscreen_pass.h"
#include "graphics/texture.h"

#include <cstdint>
#include <memory>

namespace Spot {

class GraphicsDevice;
class RenderPass;

// How light beyond what a display shows is brought within it.
enum class TonemapOperator : std::uint8_t {
    // The look of film from ACES, fitted: contrasty, with highlights that roll off and saturated colors.
    Aces,
    // Troy Sobotka's AgX: gentler, turning bright saturated light toward white as film does, so lights keep their
    // hue less but never clip into flat patches.
    AgX,
    // No curve: light above 1 clips. For views that must show colors as they are.
    Linear,
};

struct TonemapSettings {
    TonemapOperator op = TonemapOperator::Aces;
    // Multiplies the light before the curve: 2 is one stop brighter.
    float exposure = 1.0f;
    // Adds noise of under a step of 8 bits, which hides the banding of smooth gradients.
    bool dither = true;
};

// Turns an HDR image into what the display shows, with an operator and an exposure, as the last of post-processing.
// Built on FullscreenPass.
class Tonemapper {
public:
    // The format of the targets it draws into. Null, after logging why, when its resources cannot be created.
    static std::unique_ptr<Tonemapper> Create(GraphicsDevice& device, TextureFormat targetFormat);

    // Draws the HDR texture into the pass's target, which it covers.
    void Draw(RenderPass& pass, const Texture& hdr, const TonemapSettings& settings) const;

private:
    Tonemapper() = default;

    std::unique_ptr<FullscreenPass> m_pass;
    std::unique_ptr<Sampler> m_sampler;
};

} // namespace Spot
