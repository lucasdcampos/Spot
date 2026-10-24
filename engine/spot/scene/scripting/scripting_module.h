#pragma once

#include "spot/runtime/module.h"

namespace Spot {

// Scripting for an application: registers the script asset loader and the script components.
class ScriptingModule final : public Module {
protected:
    void OnStart() override;
};

} // namespace Spot
