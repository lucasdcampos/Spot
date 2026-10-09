#include "physics/physics_settings.h"

#include "physics/physics_world.h"
#include "scene/archive.h"
#include "scene/scene.h"

#include <algorithm>

namespace Spot {

static_assert(PhysicsSettings{}.gravity == PhysicsWorld::DefaultGravity);
static_assert(PhysicsSettings{}.fixedTimeStep == Scene::DefaultFixedTimeStep);

void PhysicsSettings::Serialize(Archive& archive) {
    archive.Field("gravity", gravity, {.speed = 0.05f, .tooltip = "In units per second squared"});
    archive.Field("fixedTimeStep", fixedTimeStep,
                  {.min = 0.001f,
                   .max = 0.1f,
                   .speed = 0.0005f,
                   .tooltip = "The seconds each fixed step simulates; fixed updates and physics run once per step"});
}

void PhysicsSettings::Apply(Scene& scene) const {
    // Files edited elsewhere may hold any number, and a step must be positive.
    scene.SetFixedTimeStep(std::max(fixedTimeStep, 0.0001f));
    // Physics keeps being created only when needed: when it is not, it starts with the default gravity.
    if (gravity != PhysicsWorld::DefaultGravity || scene.GetSystem<PhysicsWorld>()) {
        scene.GetPhysics().SetGravity(gravity);
    }
}

} // namespace Spot
