#pragma once

#include "spot/runtime/module.h"

namespace Spot {

// Animation for an application: registers the animation asset loaders and the animation components.
class AnimationModule final : public Module {
protected:
    void OnStart() override;
};

} // namespace Spot
