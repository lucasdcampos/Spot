#include "spot/scene/scripting/scripting_components.h"

#include "spot/scene/component_registry.h"
#include "spot/scene/entity.h"
#include "spot/scene/scripting/script_component.h"

namespace Spot {

void RegisterScriptComponents() {
    RegisterComponent<ScriptComponent>("ScriptComponent");
}

} // namespace Spot
