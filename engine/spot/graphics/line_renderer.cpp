#include "spot/graphics/line_renderer.h"

#include "spot/core/assert.h"
#include "spot/core/log.h"
#include "spot/core/profiler.h"
#include "spot/graphics/buffer.h"
#include "spot/graphics/debug_draw.h"
#include "spot/graphics/graphics_device.h"
#include "spot/graphics/pipeline.h"
#include "spot/graphics/render_context.h"

#include "line.frag.h"
#include "line.vert.h"

#include <cstddef>
#include <span>

namespace Spot {

LineRenderer::LineRenderer(GraphicsDevice& device, const LineRendererConfig& config) : m_device(device) {
    const auto vertexShader = device.CreateShader(Detail::Shaders::LineVert);
    const auto fragmentShader = device.CreateShader(Detail::Shaders::LineFrag);
    if (vertexShader && fragmentShader) {
        const auto create = [&](bool depthTest) {
            return device.CreatePipeline({
                .vertexShader = vertexShader.get(),
                .fragmentShader = fragmentShader.get(),
                .vertexBuffers = {{
                    .stride = sizeof(Vertex),
                    .attributes = {{0, VertexFormat::Float3, offsetof(Vertex, position)},
                                   {1, VertexFormat::Float4, offsetof(Vertex, color)}},
                }},
                .primitiveType = PrimitiveType::LineList,
                .blendMode = BlendMode::Alpha,
                .targetFormat = config.targetFormat,
                // Lines never hide what is drawn after them, and lie on surfaces without flickering.
                .depth = config.depthTarget ? std::optional(DepthState{
                                                  .test = depthTest, .write = false, .compare = CompareOp::LessOrEqual})
                                            : std::nullopt,
                .sampleCount = config.sampleCount,
            });
        };
        m_tested = create(true);
        m_overlay = create(false);
    }
    if (!IsValid()) {
        Log::GetEngineLogger().Error("LineRenderer could not create its GPU resources and will not draw");
    }
}

LineRenderer::~LineRenderer() = default;

bool LineRenderer::IsValid() const {
    return m_tested && m_overlay;
}

void LineRenderer::Begin(RenderPass& pass, const Matrix4& viewProjection) {
    SPOT_ASSERT(!m_pass, "LineRenderer::Begin() called twice without End()");
    m_pass = &pass;
    m_viewProjection = viewProjection;
    m_testedVertices.clear();
    m_overlayVertices.clear();
}

void LineRenderer::DrawLine(Vector3 from, Vector3 to, Color color, bool depthTest) {
    SPOT_ASSERT(m_pass, "Draw calls must come between Begin() and End()");
    std::vector<Vertex>& vertices = depthTest ? m_testedVertices : m_overlayVertices;
    vertices.push_back({from, color});
    vertices.push_back({to, color});
}

void LineRenderer::DrawLines(std::span<const DebugLine> lines) {
    for (const DebugLine& line : lines) {
        DrawLine(line.from, line.to, line.color, line.depthTest);
    }
}

void LineRenderer::DrawDebugLines() {
    DrawLines(DebugDraw::GetLines());
}

void LineRenderer::End() {
    SPOT_PROFILE_SCOPE("LineRenderer::End");
    SPOT_ASSERT(m_pass, "LineRenderer::End() called without Begin()");
    RenderPass& pass = *m_pass;
    m_pass = nullptr;
    m_lineCount = 0;
    const std::size_t tested = m_testedVertices.size();
    const std::size_t total = tested + m_overlayVertices.size();
    if (total == 0 || !IsValid() || !EnsureCapacity(total)) {
        return;
    }

    // Overlay lines follow the tested ones in the buffer, so one upload serves both draws.
    m_testedVertices.insert(m_testedVertices.end(), m_overlayVertices.begin(), m_overlayVertices.end());
    m_device.UploadToBuffer(*m_vertexBuffer, std::as_bytes(std::span(m_testedVertices)), 0, UploadMode::Discard);
    pass.BindVertexBuffer(0, *m_vertexBuffer);
    pass.PushVertexUniforms(0, m_viewProjection);
    if (tested > 0) {
        pass.BindPipeline(*m_tested);
        pass.Draw(static_cast<std::uint32_t>(tested));
    }
    if (total > tested) {
        pass.BindPipeline(*m_overlay);
        pass.Draw(static_cast<std::uint32_t>(total - tested), 1, static_cast<std::uint32_t>(tested));
    }
    m_lineCount = static_cast<std::uint32_t>(total / 2);
}

bool LineRenderer::EnsureCapacity(std::size_t vertexCount) {
    if (vertexCount <= m_capacity) {
        return true;
    }
    std::size_t capacity = m_capacity == 0 ? 4096 : m_capacity;
    while (capacity < vertexCount) {
        capacity *= 2;
    }
    m_vertexBuffer = m_device.CreateBuffer(
        {.usage = BufferUsage::Vertex, .size = static_cast<std::uint32_t>(capacity * sizeof(Vertex))});
    m_capacity = m_vertexBuffer ? capacity : 0;
    return m_vertexBuffer != nullptr;
}

} // namespace Spot
