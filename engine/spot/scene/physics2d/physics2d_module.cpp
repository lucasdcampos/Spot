#include "spot/scene/physics2d/physics2d_module.h"

#include "spot/scene/physics2d/physics2d_components.h"

namespace Spot {

void Physics2DModule::OnStart() {
    RegisterPhysics2DComponents();
}

} // namespace Spot
