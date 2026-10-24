#pragma once

#include "spot/runtime/module.h"

namespace Spot {

// 2D physics for an application: registers the 2D physics components.
class Physics2DModule final : public Module {
protected:
    void OnStart() override;
};

} // namespace Spot
