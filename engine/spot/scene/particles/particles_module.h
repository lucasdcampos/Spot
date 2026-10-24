#pragma once

#include "spot/runtime/module.h"

namespace Spot {

// Particles for an application: registers the particle components.
class ParticlesModule final : public Module {
protected:
    void OnStart() override;
};

} // namespace Spot
