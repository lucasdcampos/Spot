#include "spot/scene/scripting/scripting_module.h"

#include "spot/assets/script_assets.h"
#include "spot/runtime/application.h"
#include "spot/scene/scripting/scripting_components.h"

namespace Spot {

void ScriptingModule::OnStart() {
    RegisterScriptAssets(GetApplication().GetAssets());
    RegisterScriptComponents();
}

} // namespace Spot
