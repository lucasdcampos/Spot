#pragma once

#include <spot.h>

// B shows the shapes of every collider in the scene with DebugDraw: green for static bodies, orange for moving ones.
class PhysicsDebugView final : public Spot::Component {
public:
    bool visible = false;

protected:
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;
};
