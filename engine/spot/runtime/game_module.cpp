#include "spot/runtime/module.h"

namespace Spot {

void Detail::StartModule(Module& module) {
    module.OnStart();
}

void Detail::StopModule(Module& module) {
    module.OnStop();
}

} // namespace Spot
