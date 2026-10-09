#pragma once

#include "math/vector.h"

#include <string_view>

namespace Spot {

class Archive;
class Scene;

// The physics section of a game's settings, which the application applies to every scene it loads.
struct PhysicsSettings {
    static constexpr std::string_view SectionName = "physics";

    // An acceleration, in units per second squared.
    Vector3 gravity = {0.0f, -9.81f, 0.0f};
    // The seconds each fixed step simulates; see Scene::SetFixedTimeStep().
    float fixedTimeStep = 1.0f / 60.0f;

    void Serialize(Archive& archive);
    void Apply(Scene& scene) const;
};

} // namespace Spot
