#include "spot/graphics/fxaa.h"

#include "spot/core/log.h"
#include "spot/graphics/graphics_device.h"
#include "spot/graphics/render_context.h"
#include "spot/math/vector.h"

#include "fxaa.frag.h"

#include <array>

namespace Spot {

std::unique_ptr<Fxaa> Fxaa::Create(GraphicsDevice& device, TextureFormat targetFormat) {
    auto fxaa = std::unique_ptr<Fxaa>(new Fxaa());
    fxaa->m_pass = FullscreenPass::Create(device, Detail::Shaders::FxaaFrag, {.targetFormat = targetFormat});
    // Blending along edges reads between texels.
    fxaa->m_sampler = device.CreateSampler();
    if (!fxaa->m_pass || !fxaa->m_sampler) {
        Log::GetEngineLogger().Error("Could not create FXAA");
        return nullptr;
    }
    return fxaa;
}

void Fxaa::Draw(RenderPass& pass, const Texture& image) const {
    pass.PushFragmentUniforms(0, Vector4{1.0f / static_cast<float>(image.GetWidth()),
                                         1.0f / static_cast<float>(image.GetHeight()), 0.0f, 0.0f});
    const std::array inputs{FullscreenInput{&image, m_sampler.get()}};
    m_pass->Draw(pass, inputs);
}

} // namespace Spot
