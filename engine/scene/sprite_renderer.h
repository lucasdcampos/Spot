#pragma once

#include "assets/asset_ref.h"
#include "graphics/color.h"
#include "math/vector.h"
#include "scene/component.h"

#include <utility>

namespace Spot {

class Texture;

// Shows a texture, or a plain color, on a rectangle centered on its entity, in the entity's XY plane, as 2D games
// show characters and props. Renderer2D::DrawScene() draws every active one:
//
//     auto& player = scene.CreateEntity("Player");
//     player.AddComponent<Spot::SpriteRenderer>(Spot::Assets::Load<Spot::Texture>("sprites/player.png"));
//
// Scenes save the texture by asset id; textures made in code have none, so they are saved as null.
class SpriteRenderer final : public Component {
public:
    SpriteRenderer() = default;
    explicit SpriteRenderer(AssetRef<Texture> texture, Vector2 size = {1.0f, 1.0f})
        : m_texture(std::move(texture)), m_size(size) {}

    // Without a texture, the rectangle is filled with the color.
    const AssetRef<Texture>& GetTexture() const { return m_texture; }
    void SetTexture(AssetRef<Texture> texture) { m_texture = std::move(texture); }

    // Multiplies the texture's colors; its alpha fades the sprite.
    Color GetColor() const { return m_color; }
    void SetColor(Color color) { m_color = color; }

    // In world units, before the entity's scale.
    Vector2 GetSize() const { return m_size; }
    void SetSize(Vector2 size) { m_size = size; }

    // Mirrors the texture left to right, or top to bottom.
    bool IsFlippedX() const { return m_flipX; }
    void SetFlippedX(bool flipped) { m_flipX = flipped; }
    bool IsFlippedY() const { return m_flipY; }
    void SetFlippedY(bool flipped) { m_flipY = flipped; }

    // Sprites of a greater order are drawn over those of a lesser one, whatever their positions.
    int GetOrder() const { return m_order; }
    void SetOrder(int order) { m_order = order; }

protected:
    void Serialize(Archive& archive) override;

private:
    AssetRef<Texture> m_texture;
    Color m_color = Color::White;
    Vector2 m_size{1.0f, 1.0f};
    bool m_flipX = false;
    bool m_flipY = false;
    int m_order = 0;
};

} // namespace Spot
