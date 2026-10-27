#pragma once

#include <spot.h>

namespace Gameplay {

// The helicopter on this entity or a parent disturbs water below its main rotor, fading with distance and rotor
// speed. This visual effect is separate from flight physics and needs the water's ripple effect in the scene.
class HelicopterDownwash : public Spot::Component {
public:
    float range = 18.0f;
    float interval = 0.2f;
    float scale = 4.0f;

protected:
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    float m_untilRipple = 0.0f;
};

} // namespace Gameplay
