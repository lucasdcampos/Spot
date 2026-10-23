#include "spot/scene/physics2d/tilemap_collider_2d.h"
#include "spot/scene/entity.h"
#include "spot/scene/rendering/tilemap.h"
#include <cmath>

namespace Spot {
std::uint64_t TilemapCollider2D::GetRevision() const {
    const auto* map = GetEntity().GetComponent<Tilemap>();
    return m_revision + (map ? std::hash<ComponentId>{}(map->GetId()) + map->GetRevision() * 2 +
                                   static_cast<std::uint64_t>(map->IsActiveAndEnabled())
                             : 0);
}
void TilemapCollider2D::AppendShapes(Vector2 scale, std::vector<Shape2D>& shapes) const {
    const auto* map = GetEntity().GetComponent<Tilemap>();
    if (!map || !map->IsActiveAndEnabled())
        return;
    std::vector<TileCollisionBox> boxes;
    map->GetData().BuildCollisionBoxes(boxes);
    for (const auto& box : boxes) {
        auto shape = GetShape(scale);
        shape.center += (box.center * scale);
        shape.size = {box.size.x * std::abs(scale.x), box.size.y * std::abs(scale.y)};
        shapes.push_back(std::move(shape));
    }
}
} // namespace Spot
