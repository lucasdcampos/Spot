#pragma once

#include <spot.h>

// Lights its entity's lamp as the day cycle's daylight fades, and puts it out again by day, as street lamps do. The
// lamp's own intensity is what it reaches at night.
class NightLight final : public Spot::Component {
public:
    // At night, as Light::SetIntensity() takes it.
    float intensity = 20.0f;

protected:
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;
};
