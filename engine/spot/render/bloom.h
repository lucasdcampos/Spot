#pragma once

#include "spot/graphics/texture.h"
#include "spot/render/fullscreen_pass.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Spot {

class GraphicsDevice;
class RenderContext;

struct BloomSettings {
    // How much of the glow is added to the image; 0 turns bloom off.
    float intensity = 0.3f;
    // Light below this brightness does not glow, so only lights and what is lit brighter than white do. It fades in
    // over half of it.
    float threshold = 1.0f;
    // From 0, a tight glow, to 1, one that spreads over the whole image.
    float scatter = 0.7f;
    // Whether all light glows, as a lens spreads a share of whatever reaches it, and the glow takes the intensity's
    // share of the image in place of being added to it, so it brightens nothing on average. The threshold is ignored:
    // the brighter a light, the wider and brighter its halo looks, so a sun of real brightness gets a soft, gradual
    // halo and a lamp a faint one. Intensities of a few hundredths look like a camera's lens.
    bool conserveEnergy = false;
};

// The glow of light brighter than the display shows, as a camera's lens spreads it: the bright parts of an HDR image
// are halved again and again, then spread back up, each level adding a wider, softer glow. A Tonemapper adds the
// result to the image. Built on FullscreenPass.
class Bloom {
public:
    // The format of the images it reads, and of its own. Null, after logging why, when its resources cannot be
    // created.
    static std::unique_ptr<Bloom> Create(GraphicsDevice& device, TextureFormat format = TextureFormat::Rgba16Float);

    // Draws the glow of the image, in passes of its own, so outside any render pass. Returns it, at half the image's
    // size, to add to the image; null when the settings turn bloom off.
    const Texture* Render(RenderContext& context, const Texture& image, const BloomSettings& settings);

    static constexpr std::uint32_t MaxLevels = 7;

private:
    Bloom() = default;

    // Matches the levels to the image's size.
    bool UpdateLevels(std::uint32_t width, std::uint32_t height);

    GraphicsDevice* m_device = nullptr;
    TextureFormat m_format = TextureFormat::Rgba16Float;
    std::unique_ptr<FullscreenPass> m_down;
    std::unique_ptr<FullscreenPass> m_up;
    std::unique_ptr<Sampler> m_sampler;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    // From half the image's size down, as the downsampled and upsampled chains.
    std::vector<std::unique_ptr<Texture>> m_downLevels;
    std::vector<std::unique_ptr<Texture>> m_upLevels;
};

} // namespace Spot
