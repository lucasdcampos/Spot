#pragma once

#include <spot.h>

#include <limits>
#include <optional>

namespace Gameplay {

// What a blast is to a volume: out of it, at its surface or inside it.
enum class BlastMedium { None, Surface, Inside };

// A volume without a collider that shots and blasts still meet, such as water. What shoots or explodes asks the
// volumes through Traces, so it knows no kind of volume by name, and a new kind is one subclass.
class TraceVolume : public Spot::Component {
public:
    // How far along the ray, whose direction is a unit vector, it enters the volume within the distance; nothing when
    // it does not.
    virtual std::optional<float> Trace(Spot::Vector3 origin, Spot::Vector3 direction, float maxDistance) const = 0;
    // Meters a shot goes on inside before the volume stops it; infinity for a volume that stops nothing.
    virtual float GetShotDepth() const { return std::numeric_limits<float>::infinity(); }
    // A shot went in at the point: about 0.25 for a bullet, less for each pellet of a spread.
    virtual void OnShot(Spot::Vector3 /*point*/, float /*strength*/) {}
    // A blast of the radius went off at the center: the volume reacts to it, as water throws up a plume, and tells
    // what it was to it.
    virtual BlastMedium OnBlast(Spot::Vector3 /*center*/, float /*radius*/) { return BlastMedium::None; }
};

namespace Traces {

struct VolumeHit {
    TraceVolume* volume = nullptr;
    Spot::Vector3 point;
    float distance = 0.0f;
};

// The nearest enabled volume the ray enters within the distance.
std::optional<VolumeHit> Raycast(Spot::Scene& scene, Spot::Vector3 origin, Spot::Vector3 direction, float maxDistance);
// Tells every enabled volume of the blast, and returns what it was to them: inside one before at the surface of one.
BlastMedium Blast(Spot::Scene& scene, Spot::Vector3 center, float radius);

} // namespace Traces

} // namespace Gameplay
