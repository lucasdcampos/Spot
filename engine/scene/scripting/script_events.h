#pragma once

#include "scene/scripting/script_bindings.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace Spot {

class Entity;
class Scene;

// Sends a named event to the scripts subscribed to it, as their `Emit` does: scene-wide subscribers
// (`scene.On(name, callback)`) for the scene, or the entity's (`entity.On(name, callback)`) for an entity. Callbacks
// run synchronously, in subscription order, with the arguments; one that throws faults its own component. Inactive
// subscribers are skipped. Returns how many callbacks ran.
std::size_t EmitScriptEvent(Scene& scene, std::string_view name, std::span<const ScriptValue> arguments = {});
std::size_t EmitScriptEvent(Entity& entity, std::string_view name, std::span<const ScriptValue> arguments = {});

} // namespace Spot
