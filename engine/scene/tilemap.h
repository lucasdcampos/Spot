#pragma once

#include "graphics/color.h"
#include "graphics/sprite_atlas.h"
#include "scene/component.h"

namespace Spot {
struct TileDefinition {
    int sprite = 0;
    bool solid = false;
    void Serialize(Archive& archive);
};
struct TileCollisionBox {
    Vector2 center;
    Vector2 size;
};
// Row-major cells, with (0,0) at the bottom-left. -1 means empty; other values index definitions.
struct TilemapData {
    static constexpr int MaxDimension = 256;
    int width = 16;
    int height = 16;
    Vector2 cellSize = {1, 1};
    std::vector<TileDefinition> definitions = {{0, false}};
    std::vector<int> cells = std::vector<int>(256, -1);
    bool IsValid() const;
    int GetCell(int x, int y) const;
    bool SetCell(int x, int y, int tile);
    bool Resize(int newWidth, int newHeight);
    // Merges matching solid row runs vertically. Out is replaced and retains capacity.
    void BuildCollisionBoxes(std::vector<TileCollisionBox>& out) const;
    void Serialize(Archive& archive);
};
class Tilemap final : public Component {
public:
    const TilemapData& GetData() const { return m_data; }
    bool SetData(TilemapData data);
    bool SetCell(int x, int y, int tile);
    int GetCell(int x, int y) const { return m_data.GetCell(x, y); }
    bool Resize(int width, int height);
    const AssetRef<SpriteAtlas>& GetAtlas() const { return m_atlas; }
    void SetAtlas(AssetRef<SpriteAtlas> atlas) {
        m_atlas = std::move(atlas);
        ++m_revision;
    }
    int GetOrder() const { return m_order; }
    void SetOrder(int order) { m_order = order; }
    Color GetColor() const { return m_color; }
    void SetColor(Color color) { m_color = color; }
    std::uint64_t GetRevision() const { return m_revision; }

protected:
    void Serialize(Archive& archive) override;
    void OnValidate() override;

private:
    TilemapData m_data;
    AssetRef<SpriteAtlas> m_atlas;
    Color m_color = Color::White;
    int m_order = 0;
    std::uint64_t m_revision = 1;
};
} // namespace Spot
