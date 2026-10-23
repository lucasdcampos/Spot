#include "spot/graphics/fullscreen_pass.h"

#include "spot/core/log.h"
#include "spot/graphics/graphics_device.h"
#include "spot/graphics/render_context.h"

#include "fullscreen.vert.h"

namespace Spot {

std::unique_ptr<FullscreenPass> FullscreenPass::Create(GraphicsDevice& device, const ShaderDesc& fragmentShader,
                                                       const FullscreenPassDesc& desc) {
    auto pass = std::unique_ptr<FullscreenPass>(new FullscreenPass());
    pass->m_vertexShader = device.CreateShader(Detail::Shaders::FullscreenVert);
    pass->m_fragmentShader = device.CreateShader(fragmentShader);
    if (!pass->m_vertexShader || !pass->m_fragmentShader) {
        Log::GetEngineLogger().Error("Could not create the shaders of a fullscreen pass");
        return nullptr;
    }
    pass->m_pipeline = device.CreatePipeline({.vertexShader = pass->m_vertexShader.get(),
                                              .fragmentShader = pass->m_fragmentShader.get(),
                                              .blendMode = desc.blendMode,
                                              .targetFormat = desc.targetFormat,
                                              .sampleCount = desc.sampleCount});
    if (!pass->m_pipeline) {
        Log::GetEngineLogger().Error("Could not create the pipeline of a fullscreen pass");
        return nullptr;
    }
    return pass;
}

void FullscreenPass::Draw(RenderPass& pass, std::span<const FullscreenInput> inputs) const {
    pass.BindPipeline(*m_pipeline);
    for (std::uint32_t slot = 0; slot < inputs.size(); ++slot) {
        if (inputs[slot].texture && inputs[slot].sampler) {
            pass.BindFragmentTexture(slot, *inputs[slot].texture, *inputs[slot].sampler);
        }
    }
    pass.Draw(3);
}

} // namespace Spot
