#include "physics/physics_settings.h"

#include "core/archive.h"
#include "physics/physics_world.h"
#include "physics2d/physics_world_2d.h"
#include "scene/scene.h"

#include <algorithm>

namespace Spot {

static_assert(Entity::LayerCount == 32, "Layer masks are 32 bits");

std::vector<std::string> PhysicsSettings::MakeDefaultLayerNames() {
    std::vector<std::string> names(Entity::LayerCount);
    names[0] = "Default";
    return names;
}

bool PhysicsSettings::Collides(int a, int b) const {
    return ((layerCollisions[static_cast<std::size_t>(a)] >> b) & 1u) != 0;
}

void PhysicsSettings::SetCollides(int a, int b, bool collides) {
    const auto set = [this, collides](int layer, int other) {
        std::uint32_t& mask = layerCollisions[static_cast<std::size_t>(layer)];
        mask = collides ? mask | (1u << other) : mask & ~(1u << other);
    };
    set(a, b);
    set(b, a);
}

std::optional<int> PhysicsSettings::FindLayer(std::string_view name) const {
    const auto found = std::ranges::find(layerNames, name);
    if (name.empty() || found == layerNames.end()) {
        return std::nullopt;
    }
    return static_cast<int>(found - layerNames.begin());
}

void PhysicsSettings::Serialize(Archive& archive) {
    archive.Field("gravity", gravity, {.speed = 0.05f, .tooltip = "In units per second squared"});
    archive.Field("gravity2D", gravity2D, {.speed = 0.05f});
    archive.Field("fixedTimeStep", fixedTimeStep,
                  {.min = 0.001f,
                   .max = 0.1f,
                   .speed = 0.0005f,
                   .tooltip = "The seconds each fixed step simulates; fixed updates and physics run once per step"});
    // Tools edit the layers as a list of names and a matrix of their own.
    archive.Field("layerNames", layerNames, {.hidden = true});
    archive.Field("layerCollisions", layerCollisions, {.hidden = true});
    if (archive.IsLoading()) {
        // Files edited elsewhere may hold any number of either, and a matrix that is not symmetric.
        layerNames.resize(Entity::LayerCount);
        layerCollisions.resize(Entity::LayerCount, AllLayers);
        for (int a = 0; a < Entity::LayerCount; ++a) {
            for (int b = a + 1; b < Entity::LayerCount; ++b) {
                SetCollides(a, b, Collides(a, b) && Collides(b, a));
            }
        }
    }
}

void PhysicsSettings::Apply(Scene& scene) const {
    const bool defaults2D = gravity2D == PhysicsWorld2D::DefaultGravity &&
                            std::ranges::all_of(layerCollisions, [](std::uint32_t mask) { return mask == AllLayers; });
    if (!defaults2D || scene.GetSystem<PhysicsWorld2D>()) {
        auto& physics = scene.GetPhysics2D();
        physics.SetGravity(gravity2D);
        for (int layer = 0; layer < 32; ++layer)
            physics.SetLayerMask(layer, layer < static_cast<int>(layerCollisions.size())
                                            ? layerCollisions[static_cast<std::size_t>(layer)]
                                            : AllLayers);
    }
    // Files edited elsewhere may hold any number, and a step must be positive.
    scene.SetFixedTimeStep(std::max(fixedTimeStep, 0.0001f));
    // Physics keeps being created only when needed: when it is not, it starts with the defaults.
    const bool defaults = gravity == PhysicsWorld::DefaultGravity &&
                          std::ranges::all_of(layerCollisions, [](std::uint32_t mask) { return mask == AllLayers; }) &&
                          layerNames == MakeDefaultLayerNames();
    if (!defaults || scene.GetSystem<PhysicsWorld>()) {
        PhysicsWorld& physics = scene.GetPhysics();
        physics.SetGravity(gravity);
        physics.SetLayerNames(layerNames);
        for (int layer = 0; layer < Entity::LayerCount; ++layer) {
            physics.SetLayerMask(layer, layerCollisions[static_cast<std::size_t>(layer)]);
        }
    }
}

} // namespace Spot
