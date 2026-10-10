#pragma once

#include <spot.h>

// Physics Debug shows what physics sees, with the physics world's debug view: the colliders, where bodies touch and the
// queries the game asks.
class PhysicsDebugView final : public Spot::Component {
public:
    bool visible = false;

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    void Apply();
};
