#include "graphics/renderer_2d.h"

#include "core/profiler.h"

#include "core/assert.h"
#include "core/log.h"
#include "graphics/font.h"
#include "graphics/graphics_device.h"
#include "graphics/render_context.h"
#include "graphics/text_layout.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/sprite_renderer.h"
#include "scene/text_renderer.h"
#include "scene/tilemap.h"

#include "sprite.frag.h"
#include "sprite.vert.h"

#include <algorithm>
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
            .sampleCount = config.sampleCount,
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
    m_worldDepth = false;
    m_batch.Clear();
    m_stats = {};
    m_scissor.reset();
}

void Renderer2D::Begin(RenderPass& pass, const Matrix4& viewProjection) {
    SPOT_ASSERT(!m_pass, "Renderer2D::Begin() called twice without End()");
    m_pass = &pass;
    m_viewProjection = viewProjection;
    m_worldDepth = true;
    m_batch.Clear();
    m_stats = {};
    m_scissor.reset();
}

void Renderer2D::DrawScene(Scene& scene) {
    SPOT_PROFILE_SCOPE("Renderer2D::DrawScene");
    m_sceneDraws.clear();
    scene.ForEachEntity([&](Entity& entity) {
        if (!entity.IsActiveInHierarchy())
            return;
        const auto& transform = entity.GetTransform();
        const auto scale = Decompose(transform.GetWorldMatrix()).scale;
        entity.ForEachComponent<Component>([&](Component& component) {
            if (!component.IsActiveAndEnabled())
                return;
            if (const auto* sprite = dynamic_cast<SpriteRenderer*>(&component)) {
                Vector2 size{sprite->GetSize().x * scale.x, sprite->GetSize().y * scale.y};
                if (sprite->IsFlippedX())
                    size.x = -size.x;
                if (sprite->IsFlippedY())
                    size.y = -size.y;
                m_sceneDraws.push_back({sprite, nullptr, transform.GetWorldPosition(), transform.GetWorldRotation2D(),
                                        size, sprite->GetOrder(), m_sceneDraws.size()});
            } else if (const auto* map = dynamic_cast<Tilemap*>(&component)) {
                m_sceneDraws.push_back({nullptr,
                                        map,
                                        transform.GetWorldPosition(),
                                        transform.GetWorldRotation2D(),
                                        {scale.x, scale.y},
                                        map->GetOrder(),
                                        m_sceneDraws.size()});
            }
        });
    });
    std::ranges::sort(m_sceneDraws, [](const SceneDraw& a, const SceneDraw& b) {
        if (a.order != b.order)
            return a.order < b.order;
        if (a.position.z != b.position.z)
            return a.position.z < b.position.z;
        return a.index < b.index;
    });
    const auto projection = m_viewProjection;
    float depth = 0;
    for (const auto& item : m_sceneDraws) {
        const float nextDepth = m_worldDepth ? item.position.z : 0;
        if (nextDepth != depth) {
            Flush();
            m_batch.Clear();
            depth = nextDepth;
            m_viewProjection = projection * Matrix4::Translation({0, 0, depth});
        }
        if (item.sprite) {
            const Vector2 position{item.position.x, item.position.y};
            if (const auto* texture = item.sprite->GetDrawTexture())
                DrawRotatedQuad(position, item.size, item.rotation, *texture, item.sprite->GetColor(),
                                item.sprite->GetRegion());
            else
                DrawRotatedQuad(position, item.size, item.rotation, item.sprite->GetColor());
        } else {
            const auto& data = item.tilemap->GetData();
            const auto* atlas = item.tilemap->GetAtlas().Get();
            const auto* texture = atlas ? atlas->texture.Get() : nullptr;
            const auto& matrix = item.tilemap->GetEntity().GetTransform().GetWorldMatrix();
            for (int y = 0; y < data.height; ++y)
                for (int x = 0; x < data.width; ++x) {
                    const int tile = data.GetCell(x, y);
                    if (tile < 0 || static_cast<std::size_t>(tile) >= data.definitions.size())
                        continue;
                    const int frame = data.definitions[static_cast<std::size_t>(tile)].sprite;
                    if (atlas && (frame < 0 || static_cast<std::size_t>(frame) >= atlas->sprites.size()))
                        continue;
                    const auto world = matrix * Vector4{(static_cast<float>(x) + 0.5f) * data.cellSize.x,
                                                        (static_cast<float>(y) + 0.5f) * data.cellSize.y, 0, 1};
                    const Vector2 position{world.x, world.y};
                    if (texture)
                        DrawRotatedQuad(position, data.cellSize * item.size, item.rotation, *texture,
                                        item.tilemap->GetColor(), atlas->GetRegion(frame));
                    else
                        DrawRotatedQuad(position, data.cellSize * item.size, item.rotation, item.tilemap->GetColor());
                }
        }
    }
    if (depth != 0) {
        Flush();
        m_batch.Clear();
    }
    m_viewProjection = projection;
    m_sceneDraws.clear();
}

void Renderer2D::DrawWorldText(Scene& scene, const Camera3D& camera) {
    SPOT_PROFILE_SCOPE("Renderer2D::DrawWorldText");
    std::vector<TextRenderer*> texts;
    scene.ForEachComponent<TextRenderer>([&](TextRenderer& text) { texts.push_back(&text); });
    std::stable_sort(texts.begin(), texts.end(),
                     [](const auto* a, const auto* b) { return a->GetOrder() < b->GetOrder(); });
    const auto projection = m_viewProjection;
    for (const auto* text : texts) {
        if (!text->GetFont())
            continue;
        Flush();
        m_batch.Clear();
        m_viewProjection = projection * text->GetDrawMatrix(camera);
        DrawString(*text->GetFont(), text->GetText(), {}, text->GetColor(), text->GetPixelScale());
    }
    Flush();
    m_batch.Clear();
    m_viewProjection = projection;
}

void Renderer2D::End() {
    SPOT_PROFILE_SCOPE("Renderer2D::End");
    SPOT_ASSERT(m_pass, "Renderer2D::End() called without Begin()");
    Flush();
    m_pass = nullptr;
    Profiler::Count("2D draw calls", m_stats.drawCalls);
    Profiler::Count("2D triangles", static_cast<double>(m_stats.quadCount) * 2.0);
}

void Renderer2D::DrawQuad(Vector2 position, Vector2 size, Color color) {
    DrawRotatedQuad(position, size, 0.0f, color);
}

void Renderer2D::DrawQuad(Vector2 position, Vector2 size, const Texture& texture, Color tint,
                          const TextureRegion& region) {
    DrawRotatedQuad(position, size, 0.0f, texture, tint, region);
}

void Renderer2D::DrawRotatedQuad(Vector2 position, Vector2 size, float rotation, Color color) {
    SPOT_ASSERT(m_pass, "Draw calls must come between Begin() and End()");
    m_batch.AddQuad(position, size, rotation, nullptr, color);
}

void Renderer2D::DrawRotatedQuad(Vector2 position, Vector2 size, float rotation, const Texture& texture, Color tint,
                                 const TextureRegion& region) {
    SPOT_ASSERT(m_pass, "Draw calls must come between Begin() and End()");
    m_batch.AddQuad(position, size, rotation, &texture, tint, region);
}

void Renderer2D::DrawString(const Font& font, std::string_view text, Vector2 position, Color color, float scale) {
    SPOT_ASSERT(m_pass, "Draw calls must come between Begin() and End()");
    DrawGlyphs(font, LayoutText(font.GetAtlas(), text), position, color, scale);
}

void Renderer2D::DrawGlyphs(const Font& font, std::span<const GlyphQuad> glyphs, Vector2 position, Color color,
                            float scale) {
    SPOT_ASSERT(m_pass, "Draw calls must come between Begin() and End()");
    for (const auto& glyph : glyphs) {
        const Vector2 center = position + (glyph.min + glyph.max) * (scale / 2.0f);
        const Vector2 size = (glyph.max - glyph.min) * scale;
        m_batch.AddQuad(center, size, 0.0f, &font.GetTexture(), color, {glyph.uvMin, glyph.uvMax}, true);
    }
}

void Renderer2D::SetScissor(const ScissorRect& rect) {
    SPOT_ASSERT(m_pass, "Scissor calls must come between Begin() and End()");
    if (m_scissor && m_scissor->x == rect.x && m_scissor->y == rect.y && m_scissor->width == rect.width &&
        m_scissor->height == rect.height)
        return;
    Flush();
    m_batch.Clear();
    m_pass->SetScissor(rect);
    m_scissor = rect;
}

void Renderer2D::Flush() {
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
        m_pass->PushFragmentUniforms(0, Vector4{command.signedDistance ? 1.0f : 0.0f, 0, 0, 0});
        m_pass->BindFragmentTexture(0, command.texture ? *command.texture : *m_whiteTexture, *m_sampler);
        m_pass->DrawIndexed(command.quadCount * QuadBatch::IndicesPerQuad, 1,
                            command.firstQuad * QuadBatch::IndicesPerQuad);
    }

    m_stats.quadCount += m_batch.GetQuadCount();
    m_stats.drawCalls += static_cast<std::uint32_t>(m_batch.GetCommands().size());
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
