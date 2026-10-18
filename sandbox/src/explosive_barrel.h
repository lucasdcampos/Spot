#pragma once

#include <spot.h>

// Blows up when shot or caught in an explosion, after a short fuse so explosions spread from barrel to barrel.
class ExplosiveBarrel final : public Spot::Component {
public:
    float radius = 6.0f;
    // In newton seconds, at the center.
    float impulse = 600.0f;
    // What it takes from the health of a player at its center, less farther.
    float damage = 120.0f;
    // Seconds between being set off and exploding.
    float fuse = 0.15f;

    bool IsLit() const { return m_lit; }
    void Ignite();

protected:
    void OnStart() override;
    void Serialize(Spot::Archive& archive) override;

private:
    void ScheduleFuse(float seconds);
    void Explode();
    Spot::InvokeId m_fuse = Spot::InvokeId::Invalid;
    float m_savedFuse = -1.0f;
    bool m_lit = false;
};
