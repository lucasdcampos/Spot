#pragma once

// Luau scripts on components. Only builds configured with SPOT_SCRIPTING have it; <spot.h> leaves it out.

#if !SPOT_SCRIPTING
#error "<spot/scripting.h> needs a build configured with SPOT_SCRIPTING"
#endif

#include "spot/scene/scripting/script_api.h"
#include "spot/scene/scripting/script_component.h"
#include "spot/scene/scripting/scripting_components.h"
#include "spot/scene/scripting/scripting_module.h"
