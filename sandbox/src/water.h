#pragma once

#include <spot.h>

#include <optional>
#include <vector>

class Water;

// Where a ray going down met a water's surface.
struct WaterHit {
    Water* water = nullptr;
    Spot::Vector3 point = {};
    float distance = 0.0f;
};

// A body of water, its surface at its entity's height across the unit square on its XZ plane that its entity's scale
// stretches, as BuiltInAssets::PlaneMesh draws it, and as deep as its depth below. It has no collider, so things fall
// in: they splash as hard as they hit it, more for heavier ones, and are slowed while under. Shots and explosions that
// reach it splash it too (see WeaponHolder, Projectile and Explosion).
class Water final : public Spot::Component {
public:
    // Meters below the surface the water reaches, so far as it slows what is in it.
    float depth = 4.0f;
    // Meters a shot goes on under the surface before the water stops it, pushing less the deeper it hits.
    float shotDepth = 1.0f;
    // The share of their speed what moves under the water loses each second.
    float drag = 1.5f;
    // Meters per second something must fall in at to splash.
    float minSplashSpeed = 1.0f;

    // The water whose surface is above or below the point, if any.
    static Water* FindAt(Spot::Scene& scene, Spot::Vector3 point);
    // The nearest surface a ray from above reaches within the distance; rays from under the water never do.
    static std::optional<WaterHit> Raycast(Spot::Scene& scene, Spot::Vector3 origin, Spot::Vector3 direction,
                                           float maxDistance);

    float GetHeight() const;
    // Whether the point is above or below the surface.
    bool Covers(Spot::Vector3 point) const;
    // Whether the point is in the water: below its surface and above its depth.
    bool Contains(Spot::Vector3 point) const;
    // Splashes the surface at the point: droplets, spray and rings spreading, and a sound. A shot is about 0.25, a
    // crate falling in from a few meters about 1.5, and explosions 4 and more.
    void Splash(Spot::Vector3 point, float strength);

protected:
    void OnFixedUpdate(float fixedDeltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    // The world's axes of the surface's square, as long as its sides.
    Spot::Vector3 GetAxisX() const;
    Spot::Vector3 GetAxisZ() const;

    // What was in the water last step, so what enters is told apart.
    std::vector<Spot::EntityId> m_inside;
    std::vector<Spot::EntityId> m_insideNow;
};
