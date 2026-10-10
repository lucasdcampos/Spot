#pragma once

#include "graphics/pipeline.h"
#include "graphics/shader.h"
#include "graphics/texture.h"

#include <cstdint>
#include <memory>
#include <span>

namespace Spot {

class GraphicsDevice;
class RenderPass;

struct FullscreenPassDesc {
    // The format of the targets it draws into.
    TextureFormat targetFormat = TextureFormat::Rgba8;
    BlendMode blendMode = BlendMode::None;
    std::uint32_t sampleCount = 1;
};

// A texture and the sampler that reads it, at the fragment shader's register t{index} of a fullscreen draw.
struct FullscreenInput {
    const Texture* texture = nullptr;
    const Sampler* sampler = nullptr;
};

// Runs a fragment shader over every pixel of a pass's target, as post-processing effects do: one triangle that covers
// the target, with texture coordinates from (0, 0) at its top-left corner to (1, 1) at its bottom-right one. The
// fragment shader's input is SV_Position, then `float2 uv : TEXCOORD0`:
//
//     struct Input {
//         float4 position : SV_Position;
//         float2 uv : TEXCOORD0;
//     };
//
// Built on the public graphics API alone, like any game's code could be.
class FullscreenPass {
public:
    // Null, after logging why, when the shader or the pipeline cannot be created.
    static std::unique_ptr<FullscreenPass> Create(GraphicsDevice& device, const ShaderDesc& fragmentShader,
                                                  const FullscreenPassDesc& desc = {});

    // Draws in the pass, which must draw into targets of the pass's format and sample count, reading the inputs.
    // Uniforms pushed to the fragment shader before stay set.
    void Draw(RenderPass& pass, std::span<const FullscreenInput> inputs = {}) const;

    const Pipeline& GetPipeline() const { return *m_pipeline; }

private:
    FullscreenPass() = default;

    std::unique_ptr<Shader> m_vertexShader;
    std::unique_ptr<Shader> m_fragmentShader;
    std::unique_ptr<Pipeline> m_pipeline;
};

} // namespace Spot
