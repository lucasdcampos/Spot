#include "spot/scene/physics/physics_module.h"

#include "spot/core/debug/profiler.h"
#include "spot/runtime/application.h"
#include "spot/runtime/console.h"
#include "spot/scene/physics/physics_components.h"
#include "spot/scene/physics/physics_world.h"

#include <cmath>

namespace Spot {

PhysicsModule::PhysicsModule() = default;
PhysicsModule::~PhysicsModule() = default;

void PhysicsModule::OnStart() {
    RegisterPhysicsComponents();
    Application& application = GetApplication();
    Console& console = Console::Get();
    m_console.push_back(console.RegisterVariable(
        "physics.gravity", "Main scene gravity on the Y axis", ConsoleFlags::Cheat, FormatConsoleValue(-9.81f),
        [&application] {
            Scene* scene = application.GetScene();
            return FormatConsoleValue(scene ? scene->GetOrAddSystem<PhysicsWorld>().GetGravity().y : -9.81f);
        },
        [&application](std::string_view text) {
            float value = 0.0f;
            if (!ParseConsoleValue(text, value) || std::abs(value) > 1000.0f) {
                return false;
            }
            if (Scene* scene = application.GetScene()) {
                auto& physics = scene->GetOrAddSystem<PhysicsWorld>();
                Vector3 gravity = physics.GetGravity();
                gravity.y = value;
                physics.SetGravity(gravity);
            }
            return true;
        }));
    m_console.push_back(console.RegisterVariable(
        "physics.debug", "Draw collision shapes in the main scene", {}, FormatConsoleValue(false),
        [&application] {
            Scene* scene = application.GetScene();
            return FormatConsoleValue(scene &&
                                      scene->GetOrAddSystem<PhysicsWorld>().GetDebugDraw() != PhysicsDebugFlags::None);
        },
        [&application](std::string_view text) {
            bool value = false;
            if (!ParseConsoleValue(text, value)) {
                return false;
            }
            if (Scene* scene = application.GetScene()) {
                scene->GetOrAddSystem<PhysicsWorld>().SetDebugDraw(value ? PhysicsDebugFlags::Colliders
                                                                         : PhysicsDebugFlags::None);
            }
            return true;
        }));
}

void PhysicsModule::OnStop() {
    m_console.clear();
}

void PhysicsModule::OnUpdate(float /*deltaTime*/) {
    if (!Profiler::IsEnabled()) {
        return;
    }
    const Scene* scene = GetApplication().GetScene();
    const PhysicsWorld* physics = scene ? scene->GetSystem<PhysicsWorld>() : nullptr;
    Profiler::Count("Bodies", physics ? static_cast<double>(physics->GetBodyCount()) : 0.0);
}

} // namespace Spot
