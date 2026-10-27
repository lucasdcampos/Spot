#include <spot.h>

#include "gameplay/environment/day_cycle.h"

namespace {

// The sandbox's code: its components, and the script bindings of the day cycle.
class SandboxGame final : public Spot::GameCodeModule {
protected:
    void OnStart() override {
        GameCodeModule::OnStart();
        Gameplay::DayCycle::RegisterScriptBindings();
    }
};

} // namespace

SPOT_GAME_MODULE(SandboxGame)
