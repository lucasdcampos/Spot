#include "physics_debug_view.h"
#include <spot/scene/physics/physics_world.h>

SPOT_COMPONENT(PhysicsDebugView);

void PhysicsDebugView::OnStart() {
    Apply();
}

void PhysicsDebugView::OnUpdate(float /*deltaTime*/) {
    if (Spot::Input::IsActionPressed("Physics Debug")) {
        visible = !visible;
        Apply();
    }
}

void PhysicsDebugView::OnDisable() {
    GetScene().GetOrAddSystem<Spot::PhysicsWorld>().SetDebugDraw(Spot::PhysicsDebugFlags::None);
}

void PhysicsDebugView::Apply() {
    GetScene().GetOrAddSystem<Spot::PhysicsWorld>().SetDebugDraw(visible ? Spot::PhysicsDebugFlags::All
                                                                         : Spot::PhysicsDebugFlags::None);
}

void PhysicsDebugView::Serialize(Spot::Archive& archive) {
    archive.Field("visible", visible);
}
