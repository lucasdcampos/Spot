#include "spot/scene/rendering/sprite_renderer.h"

#include "spot/assets/asset_archive.h"
#include "spot/core/io/archive.h"
#include "spot/graphics/texture.h"

namespace Spot {

const Texture* SpriteRenderer::GetDrawTexture() const {
    return m_atlas ? m_atlas->texture.Get() : m_texture.Get();
}
TextureRegion SpriteRenderer::GetRegion() const {
    return m_atlas ? m_atlas->GetRegion(m_frame) : TextureRegion{};
}

void SpriteRenderer::Serialize(Archive& archive) {
    archive.Field("texture", m_texture);
    archive.Field("atlas", m_atlas);
    archive.Field("frame", m_frame);
    archive.Field("color", m_color, {.tooltip = "Multiplies the texture's colors"});
    archive.Field("size", m_size, {.min = 0.0f, .max = 100000.0f, .speed = 0.01f, .tooltip = "In world units"});
    archive.Field("flipX", m_flipX, {.tooltip = "Mirrors the texture left to right"});
    archive.Field("flipY", m_flipY, {.tooltip = "Mirrors the texture top to bottom"});
    archive.Field("order", m_order, {.tooltip = "Sprites of a greater order are drawn over the others"});
}

} // namespace Spot
