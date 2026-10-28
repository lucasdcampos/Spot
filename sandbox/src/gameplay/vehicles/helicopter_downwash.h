#pragma once

#include <spot.h>

namespace Gameplay {

// The helicopter on this entity or a parent drives branch wind and water ripples below its rotor.
// Both fade with distance and rotor speed; water ripples use the scene's water effect.
class HelicopterDownwash : public Spot::Component {
public:
    float range = 18.0f;
    float interval = 0.2f;
    float scale = 4.0f;

protected:
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    float m_untilRipple = 0.0f;
};

} // namespace Gameplay
