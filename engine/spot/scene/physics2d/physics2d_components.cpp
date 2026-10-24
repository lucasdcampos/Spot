#include "spot/scene/physics2d/physics2d_components.h"

#include "spot/scene/component_registry.h"
#include "spot/scene/physics2d/physics_world_2d.h"
#include "spot/scene/physics2d/tilemap_collider_2d.h"

namespace Spot {

void RegisterPhysics2DComponents() {
    RegisterComponent<Rigidbody2D>("Rigidbody2D");
    RegisterComponent<BoxCollider2D>("BoxCollider2D");
    RegisterComponent<CircleCollider2D>("CircleCollider2D");
    RegisterComponent<CapsuleCollider2D>("CapsuleCollider2D");
    RegisterComponent<PolygonCollider2D>("PolygonCollider2D");
    RegisterComponent<TilemapCollider2D>("TilemapCollider2D");
}

} // namespace Spot
