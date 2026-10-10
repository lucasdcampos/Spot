#pragma once

#include <spot.h>

// Blows up when shot or caught in an explosion, after a short fuse so explosions spread from barrel to barrel.
class ExplosiveBarrel final : public Spot::Component {
public:
    float radius = 6.0f;
    // In newton seconds, at the center.
    float impulse = 600.0f;
    // Seconds between being set off and exploding.
    float fuse = 0.15f;

    bool IsLit() const { return m_fuseLeft >= 0.0f; }
    void Ignite();

protected:
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    float m_fuseLeft = -1.0f;
};
