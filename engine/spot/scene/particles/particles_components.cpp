#include "spot/scene/particles/particles_components.h"

#include "spot/scene/component_registry.h"
#include "spot/scene/entity.h"
#include "spot/scene/particles/particle_system.h"

namespace Spot {

void RegisterParticleComponents() {
    RegisterComponent<ParticleSystem>("ParticleSystem");
}

} // namespace Spot
