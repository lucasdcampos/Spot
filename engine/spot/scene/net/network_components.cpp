#include "spot/scene/net/network_components.h"

#include "spot/scene/component_registry.h"
#include "spot/scene/entity.h"
#include "spot/scene/net/network_object.h"
#include "spot/scene/net/network_transform.h"

namespace Spot {

void RegisterNetworkComponents() {
    RegisterComponent<NetworkObject>("NetworkObject");
    RegisterComponent<NetworkTransform>("NetworkTransform");
}

} // namespace Spot
