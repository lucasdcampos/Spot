#pragma once

#include "spot/graphics/fullscreen_pass.h"
#include "spot/graphics/image.h"
#include "spot/graphics/texture.h"

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

// Changes to the colors of an image, as a colorist makes them.
struct GradingSettings {
    // Spreads light away from middle grey, in stops, making darks darker and lights lighter; 1 keeps it as it is.
    float contrast = 1.0f;
    // 0 is grey, 1 keeps the colors, and above 1 strengthens them.
    float saturation = 1.0f;
    // From -1, cooler, bluer light, to 1, warmer, redder light, keeping the brightness.
    float temperature = 0.0f;
    // From -1, greener, to 1, more magenta.
    float tint = 0.0f;
    // How dark the corners get, from 0 to 1.
    float vignette = 0.0f;
    // Where the vignette starts darkening, from 0 at the middle of the image to 1 at its corners.
    float vignetteStart = 0.4f;
    // A color cube as a strip of its blue slices, side by side: an image of size * size by size pixels, such as 256 by
    // 16, made by grading MakeNeutralLut() in an image editor. It maps displayed (sRGB) colors to displayed colors, so
    // its texture keeps its values as they are: Rgba8, not Rgba8Srgb. None when null.
    const Texture* lut = nullptr;
    // How much of the LUT applies, from 0 to 1.
    float lutContribution = 1.0f;
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

    // Draws the HDR texture into the pass's target, which it covers, with the glow of Bloom added at its intensity,
    // and graded.
    void Draw(RenderPass& pass, const Texture& hdr, const TonemapSettings& settings,
              const GradingSettings& grading = {}, const Texture* bloom = nullptr, float bloomIntensity = 0.0f) const;

private:
    Tonemapper() = default;

    std::unique_ptr<FullscreenPass> m_pass;
    std::unique_ptr<Sampler> m_sampler;
    std::unique_ptr<Sampler> m_bloomSampler;
    std::unique_ptr<Sampler> m_lutSampler;
    // Added when there is no bloom.
    std::unique_ptr<Texture> m_black;
};

// A LUT that changes nothing, in the strip format GradingSettings::lut takes, for grading in an image editor.
Image MakeNeutralLut(std::uint32_t size = 16);

} // namespace Spot
