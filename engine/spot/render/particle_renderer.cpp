#include "spot/render/particle_renderer.h"
#include "particle.frag.h"
#include "particle.vert.h"
#include "spot/core/assert.h"
#include "spot/core/log.h"
#include "spot/core/profiler.h"
#include "spot/graphics/graphics_device.h"
#include "spot/graphics/pipeline.h"
#include <array>
#include <cstddef>

namespace Spot {
ParticleRenderer::ParticleRenderer(GraphicsDevice& device, const ParticleRendererConfig& config) : m_device(device) {
    auto vertex = device.CreateShader(Detail::Shaders::ParticleVert);
    auto fragment = device.CreateShader(Detail::Shaders::ParticleFrag);
    if (vertex && fragment) {
        PipelineDesc desc{
            .vertexShader = vertex.get(),
            .fragmentShader = fragment.get(),
            .vertexBuffers = {{.stride = sizeof(ParticleInstance),
                               .attributes = {{0, VertexFormat::Float4, offsetof(ParticleInstance, centerAndSize)},
                                              {1, VertexFormat::Float4,
                                               offsetof(ParticleInstance, rightAndSoftDistance)},
                                              {2, VertexFormat::Float4, offsetof(ParticleInstance, up)},
                                              {3, VertexFormat::Float4, offsetof(ParticleInstance, color)},
                                              {4, VertexFormat::Float4, offsetof(ParticleInstance, uvRect)}},
                               .inputRate = VertexInputRate::Instance}},
            .blendMode = BlendMode::Alpha,
            .targetFormat = config.targetFormat,
            .sampleCount = config.sampleCount};
        m_alpha = device.CreatePipeline(desc);
        desc.blendMode = BlendMode::Additive;
        m_additive = device.CreatePipeline(desc);
    }
    m_sampler = device.CreateSampler();
    m_depthSampler = device.CreateSampler({.filter = TextureFilter::Nearest});
    m_white = device.CreateTexture({.width = 1, .height = 1});
    m_noDepth = device.CreateTexture({.width = 1, .height = 1});
    if (m_white)
        device.UploadToTexture(*m_white, std::array<std::uint8_t, 4>{255, 255, 255, 255});
    if (m_noDepth)
        device.UploadToTexture(*m_noDepth, std::array<std::uint8_t, 4>{255, 255, 255, 255});
    if (!IsValid())
        Log::GetEngineLogger().Error("ParticleRenderer could not create GPU resources and will not draw");
}
ParticleRenderer::~ParticleRenderer() = default;
bool ParticleRenderer::IsValid() const {
    return m_alpha && m_additive && m_sampler && m_depthSampler && m_white && m_noDepth;
}
void ParticleRenderer::Begin(RenderContext& context, const RenderPassDesc& target, const Camera3D& camera,
                             Vector2 viewSize, const Texture* depth) {
    SPOT_ASSERT(!m_context, "ParticleRenderer already begun");
    m_context = &context;
    m_target = target;
    m_target.depthTarget = nullptr;
    m_target.clearColor.reset();
    m_target.clearDepth.reset();
    m_camera = camera;
    m_viewSize = viewSize;
    m_depth = depth && depth->GetSampleCount() == 1 ? depth : nullptr;
    m_batch.Clear();
    m_instances.clear();
    m_stats = {};
}
void ParticleRenderer::Draw(std::span<const Particle> particles, const ParticleSettings& settings,
                            const Matrix4& emitter, const Texture* texture) {
    SPOT_ASSERT(m_context, "ParticleRenderer must be begun");
    m_batch.Add(particles, settings, m_camera, emitter, texture);
}
void ParticleRenderer::End() {
    SPOT_PROFILE_SCOPE("ParticleRenderer::End");
    SPOT_ASSERT(m_context, "ParticleRenderer was not begun");
    auto* context = m_context;
    m_context = nullptr;
    if (!IsValid() || m_batch.GetDraws().empty())
        return;
    m_batch.Sort();
    const auto draws = m_batch.GetDraws();
    for (const auto& draw : draws)
        m_instances.push_back(draw.instance);
    if (m_instances.size() > m_capacity) {
        m_capacity = std::max(m_instances.size(), m_capacity * 2 + 1024);
        m_buffer = m_device.CreateBuffer(
            {.usage = BufferUsage::Vertex, .size = static_cast<std::uint32_t>(m_capacity * sizeof(ParticleInstance))});
        if (!m_buffer) {
            m_capacity = 0;
            return;
        }
    }
    m_device.UploadToBuffer(*m_buffer, m_instances, 0, UploadMode::Discard);
    auto pass = context->BeginRenderPass(m_target);
    pass.PushVertexUniforms(0, m_camera.GetViewProjection(m_viewSize));
    struct Uniforms {
        Vector4 planes;
        Vector4 view;
    };
    const Uniforms uniforms{
        {m_camera.nearPlane, m_camera.farPlane, m_camera.orthographic ? 1.0f : 0.0f, m_depth ? 1.0f : 0.0f},
        {m_viewSize.x, m_viewSize.y, 0, 0}};
    pass.BindFragmentTexture(1, m_depth ? *m_depth : *m_noDepth, *m_depthSampler);
    for (std::size_t first = 0; first < draws.size();) {
        std::size_t end = first + 1;
        while (end < draws.size() && draws[end].texture == draws[first].texture &&
               draws[end].blend == draws[first].blend)
            ++end;
        const auto& draw = draws[first];
        pass.BindPipeline(draw.blend == ParticleBlend::Alpha ? *m_alpha : *m_additive);
        pass.BindFragmentTexture(0, draw.texture ? *draw.texture : *m_white, *m_sampler);
        auto values = uniforms;
        values.view.z = draw.texture ? 1.0f : 0.0f;
        pass.PushFragmentUniforms(0, values);
        // Buffer offsets work consistently across APIs where SV_InstanceID restarts at zero.
        pass.BindVertexBuffer(0, *m_buffer, static_cast<std::uint32_t>(first * sizeof(ParticleInstance)));
        pass.Draw(6, static_cast<std::uint32_t>(end - first));
        ++m_stats.drawCalls;
        first = end;
    }
    m_stats.particles = static_cast<std::uint32_t>(draws.size());
}
} // namespace Spot
