#include "graphics/tonemapper.h"

#include "core/log.h"
#include "graphics/graphics_device.h"
#include "graphics/render_context.h"
#include "math/vector.h"

#include "tonemap.frag.h"

#include <algorithm>
#include <array>

namespace Spot {

std::unique_ptr<Tonemapper> Tonemapper::Create(GraphicsDevice& device, TextureFormat targetFormat) {
    auto tonemapper = std::unique_ptr<Tonemapper>(new Tonemapper());
    tonemapper->m_pass = FullscreenPass::Create(device, Detail::Shaders::TonemapFrag, {.targetFormat = targetFormat});
    // The HDR image is the size of the target, so each pixel reads its own texel.
    tonemapper->m_sampler = device.CreateSampler({.filter = TextureFilter::Nearest});
    if (!tonemapper->m_pass || !tonemapper->m_sampler) {
        Log::GetEngineLogger().Error("Could not create the tonemapper");
        return nullptr;
    }
    return tonemapper;
}

void Tonemapper::Draw(RenderPass& pass, const Texture& hdr, const TonemapSettings& settings) const {
    pass.PushFragmentUniforms(0, Vector4{std::max(settings.exposure, 0.0f), static_cast<float>(settings.op),
                                         settings.dither ? 1.0f : 0.0f, 0.0f});
    const std::array inputs{FullscreenInput{&hdr, m_sampler.get()}};
    m_pass->Draw(pass, inputs);
}

} // namespace Spot
