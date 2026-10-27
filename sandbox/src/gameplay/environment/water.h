#pragma once

#include "gameplay/core/trace_volume.h"

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

// A body of water, its surface at its entity's height across the unit square on its XZ plane that its entity's scale
// stretches, as BuiltInAssets::PlaneMesh draws it, and as deep as its depth below. It has no collider, so things fall
// in: bodies and characters splash as hard as they hit it, more for heavier ones, and bodies are slowed while under.
// As a TraceVolume, it stops shots soon under its surface and splashes where they go in, and takes blasts at its
// surface or under it, throwing up its water.
//
// Its splashes play the effects of the scene's EffectLibrary named after splashEffect: the effect itself, droplets
// and spray, and ".ripples", ".mist" for harder ones and ".plume" for blasts.
class Water : public TraceVolume {
public:
    // Meters below the surface the water reaches, so far as it slows what is in it.
    float depth = 4.0f;
    // Meters a shot goes on under the surface before the water stops it, pushing less the deeper it hits.
    float shotDepth = 1.0f;
    // The share of their speed what moves under the water loses each second.
    float drag = 1.5f;
    // Meters per second something must fall in at to splash.
    float minSplashSpeed = 1.0f;
    std::string splashEffect = "splash";
    // Shots and small things falling in; big things and blasts.
    Spot::AssetRef<Spot::Sound> smallSplash;
    Spot::AssetRef<Spot::Sound> largeSplash;

    // The enabled water whose surface is above or below the point, if any.
    static Water* FindAt(Spot::Scene& scene, Spot::Vector3 point);

    float GetHeight() const;
    // Whether the point is above or below the surface.
    bool Covers(Spot::Vector3 point) const;
    // Whether the point is in the water: below its surface and above its depth.
    bool Contains(Spot::Vector3 point) const;
    // Splashes the surface at the point: droplets, spray and rings spreading, and a sound. A shot is about 0.25, a
    // crate falling in from a few meters about 1.5, and explosions 4 and more.
    void Splash(Spot::Vector3 point, float strength);

    // Rays from above meet the surface; rays from under it never do.
    std::optional<float> Trace(Spot::Vector3 origin, Spot::Vector3 direction, float maxDistance) const override;
    float GetShotDepth() const override { return shotDepth; }
    void OnShot(Spot::Vector3 point, float strength) override;
    BlastMedium OnBlast(Spot::Vector3 center, float radius) override;

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

} // namespace Gameplay
