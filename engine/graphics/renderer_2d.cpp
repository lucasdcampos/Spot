#include "graphics/renderer_2d.h"

#include "core/assert.h"
#include "core/log.h"
#include "graphics/graphics_device.h"
#include "graphics/render_context.h"

#include "sprite.frag.h"
#include "sprite.vert.h"

#include <array>
#include <cstddef>
#include <vector>

namespace Spot {

Renderer2D::Renderer2D(GraphicsDevice& device, const Renderer2DConfig& config) : m_device(device) {
    const auto vertexShader = device.CreateShader(Detail::Shaders::SpriteVert);
    const auto fragmentShader = device.CreateShader(Detail::Shaders::SpriteFrag);
    if (vertexShader && fragmentShader) {
        m_pipeline = device.CreatePipeline({
            .vertexShader = vertexShader.get(),
            .fragmentShader = fragmentShader.get(),
            .vertexBuffers = {{
                .stride = sizeof(QuadVertex),
                .attributes = {{0, VertexFormat::Float2, offsetof(QuadVertex, position)},
                               {1, VertexFormat::Float2, offsetof(QuadVertex, uv)},
                               {2, VertexFormat::Float4, offsetof(QuadVertex, color)}},
            }},
            .blendMode = BlendMode::Alpha,
            .targetFormat = config.targetFormat,
        });
    }

    m_sampler = device.CreateSampler({.filter = config.filter});

    // Untextured quads sample this, so one shader and pipeline serve both kinds of quad.
    m_whiteTexture = device.CreateTexture({.width = 1, .height = 1});
    if (m_whiteTexture) {
        device.UploadToTexture(*m_whiteTexture, std::array<std::uint8_t, 4>{255, 255, 255, 255});
    }

    if (!IsValid()) {
        Log::GetEngineLogger().Error("Renderer2D could not create its GPU resources and will not draw");
    }
}

Renderer2D::~Renderer2D() = default;

bool Renderer2D::IsValid() const {
    return m_pipeline && m_sampler && m_whiteTexture;
}

void Renderer2D::Begin(RenderPass& pass, const Camera2D& camera, Vector2 viewSize) {
    SPOT_ASSERT(!m_pass, "Renderer2D::Begin() called twice without End()");
    m_pass = &pass;
    m_viewProjection = camera.GetViewProjection(viewSize);
    m_batch.Clear();
}

void Renderer2D::End() {
    SPOT_ASSERT(m_pass, "Renderer2D::End() called without Begin()");
    Flush();
    m_pass = nullptr;
}

void Renderer2D::DrawQuad(Vector2 position, Vector2 size, Color color) {
    DrawRotatedQuad(position, size, 0.0f, color);
}

void Renderer2D::DrawQuad(Vector2 position, Vector2 size, const Texture& texture, Color tint) {
    DrawRotatedQuad(position, size, 0.0f, texture, tint);
}

void Renderer2D::DrawRotatedQuad(Vector2 position, Vector2 size, float rotation, Color color) {
    SPOT_ASSERT(m_pass, "Draw calls must come between Begin() and End()");
    m_batch.AddQuad(position, size, rotation, nullptr, color);
}

void Renderer2D::DrawRotatedQuad(Vector2 position, Vector2 size, float rotation, const Texture& texture, Color tint) {
    SPOT_ASSERT(m_pass, "Draw calls must come between Begin() and End()");
    m_batch.AddQuad(position, size, rotation, &texture, tint);
}

void Renderer2D::Flush() {
    m_stats = {};
    if (m_batch.IsEmpty() || !IsValid() || !EnsureCapacity(m_batch.GetQuadCount())) {
        return;
    }

    // Discarding lets several Begin()/End() rounds in one frame reuse the buffer: draws recorded by an earlier round
    // keep reading the data they were recorded with.
    m_device.UploadToBuffer(*m_vertexBuffer, std::as_bytes(m_batch.GetVertices()), 0, UploadMode::Discard);

    m_pass->BindPipeline(*m_pipeline);
    m_pass->BindVertexBuffer(0, *m_vertexBuffer);
    m_pass->BindIndexBuffer(*m_indexBuffer, IndexType::UInt32);
    m_pass->PushVertexUniforms(0, m_viewProjection);

    for (const auto& command : m_batch.GetCommands()) {
        m_pass->BindFragmentTexture(0, command.texture ? *command.texture : *m_whiteTexture, *m_sampler);
        m_pass->DrawIndexed(command.quadCount * QuadBatch::IndicesPerQuad, 1,
                            command.firstQuad * QuadBatch::IndicesPerQuad);
    }

    m_stats.quadCount = m_batch.GetQuadCount();
    m_stats.drawCalls = static_cast<std::uint32_t>(m_batch.GetCommands().size());
}

bool Renderer2D::EnsureCapacity(std::uint32_t quadCount) {
    if (quadCount <= m_capacity) {
        return true;
    }

    std::uint32_t capacity = m_capacity == 0 ? 1024 : m_capacity;
    while (capacity < quadCount) {
        capacity *= 2;
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(capacity) * QuadBatch::IndicesPerQuad);
    for (std::uint32_t quad = 0; quad < capacity; ++quad) {
        const std::uint32_t first = quad * QuadBatch::VerticesPerQuad;
        indices.insert(indices.end(), {first, first + 1, first + 2, first + 2, first + 3, first});
    }

    m_vertexBuffer = m_device.CreateBuffer(
        {.usage = BufferUsage::Vertex,
         .size = static_cast<std::uint32_t>(capacity * QuadBatch::VerticesPerQuad * sizeof(QuadVertex))});
    m_indexBuffer = m_device.CreateBuffer(BufferUsage::Index, indices);
    if (!m_vertexBuffer || !m_indexBuffer) {
        m_capacity = 0;
        return false;
    }

    m_capacity = capacity;
    return true;
}

} // namespace Spot
