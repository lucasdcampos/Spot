#pragma once

#include "graphics/particle_batch.h"
#include "graphics/render_context.h"
#include <memory>
#include <optional>

namespace Spot {
class GraphicsDevice;
class Pipeline;
class Sampler;
class Scene;

struct ParticleRendererConfig {
    std::optional<TextureFormat> targetFormat;
    std::uint32_t sampleCount = 1;
};
struct ParticleRendererStats {
    std::uint32_t particles = 0;
    std::uint32_t drawCalls = 0;
};
class ParticleRenderer {
public:
    explicit ParticleRenderer(GraphicsDevice& device, const ParticleRendererConfig& config = {});
    ~ParticleRenderer();
    bool IsValid() const;
    // Depth is a readable single-sample camera depth texture, never attached to the particle pass.
    void Begin(RenderContext& context, const RenderPassDesc& target, const Camera3D& camera, Vector2 viewSize,
               const Texture* depth = nullptr);
    void Draw(std::span<const Particle> particles, const ParticleSettings& settings, const Matrix4& emitter = {},
              const Texture* texture = nullptr);
    void DrawScene(Scene& scene);
    void End();
    ParticleRendererStats GetStats() const { return m_stats; }

private:
    GraphicsDevice& m_device;
    std::unique_ptr<Pipeline> m_alpha, m_additive;
    std::unique_ptr<Sampler> m_sampler, m_depthSampler;
    std::unique_ptr<Texture> m_white, m_noDepth;
    std::unique_ptr<Buffer> m_buffer;
    std::size_t m_capacity = 0;
    std::vector<ParticleInstance> m_instances;
    ParticleBatch m_batch;
    RenderContext* m_context = nullptr;
    RenderPassDesc m_target;
    Camera3D m_camera;
    Vector2 m_viewSize;
    const Texture* m_depth = nullptr;
    ParticleRendererStats m_stats;
};
} // namespace Spot
