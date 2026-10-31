#pragma once

#include <spot.h>

namespace Gameplay {

// Shakes the view of its entity's pawn, as a blast nearby does, for the view to add to where it looks (GetRotation()).
// Whatever shakes the world calls Add(scene, ...), and every view within reach shakes, so a blast knows no viewer. What
// a server shakes, its clients' views shake with too.
class CameraShake : public Spot::Component {
public:
    // The most it turns the view by, in radians, however strong the shakes.
    float maxAngle = 0.2f;

    // Shakes every view within the radius of the center, by up to the strength, in radians, at the center, less and
    // shorter the farther it is.
    static void Add(Spot::Scene& scene, Spot::Vector3 center, float strength, float radius);

    // Shakes by up to the radians, dying down over the seconds. A stronger shake takes over a weaker one going on.
    void Add(float radians, float seconds = 0.6f);
    // What the shake turns the view by now, to apply after where it looks.
    Spot::Quaternion GetRotation() const;

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    float m_strength = 0.0f;
    float m_length = 0.0f;
    float m_left = 0.0f;
    float m_clock = 0.0f;
};

} // namespace Gameplay
