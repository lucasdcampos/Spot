#pragma once

#include <spot.h>

#include <string>

namespace Gameplay {

// Turns its entity, the sun, across the sky: it rises in the east (+X) at 6:00, peaks at noon and sets in the west at
// 18:00. The sky follows the sun and lights the scene through the air, so the light dims and reddens on its own as
// it sets, and the moon rises opposite it. The Fog and the PostProcessing of its entity follow: pale haze by day, warm
// at sunset and dark blue at night, the exposure rising as eyes adjust to a low sun and to moonlight. The scene's Sky
// turns its stars with the sun, and its effects are lit as much as the day is (EffectPool::SetLight()). Holding its
// fast forward action speeds time up.
class DayCycle : public Spot::Component {
public:
    // From 0 at midnight to 1 at the next one.
    float timeOfDay = 0.375f;
    // Seconds a whole day takes.
    float dayLength = 120.0f;
    // How many times faster time goes while Fast Forward is held.
    float fastForward = 20.0f;
    // The input action that speeds time up while held.
    std::string fastForwardAction = "Fast Forward";

    // Registers its script bindings, so scripts read the time of day and set it, by the component's name.
    static void RegisterScriptBindings();

    // How much daylight there is, from 0 at night to 1 once the sun is well up.
    float GetDaylight() const { return m_daylight; }
    // Applies a time in [0, 1) immediately, including the sky, fog and exposure.
    void SetTimeOfDay(float time);

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;
    // Shows the sun where the time of day puts it, in the editor too.
    void OnValidate() override;

private:
    void UpdateSun();
    // Lights the effects as much as the day is.
    void UpdateEffectLight();
    // Sets the fog and the look for how high the sun is, from -1 below to 1 above.
    void UpdateAtmosphere(float sunHeight);

    float m_daylight = 1.0f;
};

} // namespace Gameplay
