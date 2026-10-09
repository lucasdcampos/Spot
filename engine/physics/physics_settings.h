#pragma once

#include "math/vector.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Spot {

class Archive;
class Scene;

// The physics section of a game's settings, which the application applies to every scene it loads.
struct PhysicsSettings {
    static constexpr std::string_view SectionName = "physics";
    static constexpr std::uint32_t AllLayers = 0xffffffffu;

    // An acceleration, in units per second squared.
    Vector3 gravity = {0.0f, -9.81f, 0.0f};
    // The seconds each fixed step simulates; see Scene::SetFixedTimeStep().
    float fixedTimeStep = 1.0f / 60.0f;
    // The names of the entities' layers (see Entity::GetLayer()), by layer; layers without a name are unused. Always
    // Entity::LayerCount of them, the first named "Default".
    std::vector<std::string> layerNames = MakeDefaultLayerNames();
    // For each layer, a bit per layer it collides with. Always Entity::LayerCount of them, and symmetric: a layer
    // collides with another when the other collides with it. Every layer collides with every layer by default.
    std::vector<std::uint32_t> layerCollisions = std::vector<std::uint32_t>(32, AllLayers);

    static std::vector<std::string> MakeDefaultLayerNames();

    bool Collides(int a, int b) const;
    // Sets both directions, so the matrix stays symmetric.
    void SetCollides(int a, int b, bool collides);
    // The layer with the name; nothing when no layer has it.
    std::optional<int> FindLayer(std::string_view name) const;

    void Serialize(Archive& archive);
    void Apply(Scene& scene) const;
};

} // namespace Spot
