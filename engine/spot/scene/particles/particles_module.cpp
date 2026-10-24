#include "spot/scene/particles/particles_module.h"

#include "spot/scene/particles/particles_components.h"

namespace Spot {

void ParticlesModule::OnStart() {
    RegisterParticleComponents();
}

} // namespace Spot
