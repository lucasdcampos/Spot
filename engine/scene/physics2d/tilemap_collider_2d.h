#pragma once
#include "scene/physics2d/collider_2d.h"

namespace Spot {
// Generates compound boxes from the solid tile definitions on the same entity.
class TilemapCollider2D final : public Collider2D {
public:
    TilemapCollider2D() : Collider2D(ShapeKind2D::Box) {}
    std::uint64_t GetRevision() const override;
    void AppendShapes(Vector2 scale, std::vector<Shape2D>& shapes) const override;
};
} // namespace Spot
