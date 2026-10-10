#include "graphics/tonemapper.h"

#include "core/log.h"
#include "graphics/graphics_device.h"
#include "graphics/render_context.h"
#include "math/vector.h"

#include "tonemap.frag.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace Spot {

std::unique_ptr<Tonemapper> Tonemapper::Create(GraphicsDevice& device, TextureFormat targetFormat) {
    auto tonemapper = std::unique_ptr<Tonemapper>(new Tonemapper());
    tonemapper->m_pass = FullscreenPass::Create(device, Detail::Shaders::TonemapFrag, {.targetFormat = targetFormat});
    // The HDR image is the size of the target, so each pixel reads its own texel.
    tonemapper->m_sampler = device.CreateSampler({.filter = TextureFilter::Nearest});
    // The bloom is smaller than the target, and smooth, so it is filtered.
    tonemapper->m_bloomSampler = device.CreateSampler();
    tonemapper->m_black = device.CreateTexture({.width = 1, .height = 1, .format = TextureFormat::Rgba16Float});
    if (tonemapper->m_black) {
        device.UploadToTexture(*tonemapper->m_black, std::array<std::uint16_t, 4>{});
    }
    if (!tonemapper->m_pass || !tonemapper->m_sampler || !tonemapper->m_bloomSampler || !tonemapper->m_black) {
        Log::GetEngineLogger().Error("Could not create the tonemapper");
        return nullptr;
    }
    return tonemapper;
}

void Tonemapper::Draw(RenderPass& pass, const Texture& hdr, const TonemapSettings& settings, const Texture* bloom,
                      float bloomIntensity) const {
    pass.PushFragmentUniforms(0, Vector4{std::max(settings.exposure, 0.0f), static_cast<float>(settings.op),
                                         settings.dither ? 1.0f : 0.0f, bloom ? std::max(bloomIntensity, 0.0f) : 0.0f});
    const std::array inputs{FullscreenInput{&hdr, m_sampler.get()},
                            FullscreenInput{bloom ? bloom : m_black.get(), m_bloomSampler.get()}};
    m_pass->Draw(pass, inputs);
}

} // namespace Spot
