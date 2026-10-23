#include <spot.h>

#include "day_cycle.h"

#include <stdexcept>

namespace {

void RegisterScriptBindings() {
    Spot::ScriptBindings<DayCycle>()
        .ReadOnly("timeOfDay", [](const DayCycle& cycle) { return cycle.timeOfDay; })
        .ReadOnly("daylight", &DayCycle::GetDaylight)
        .Method("GetDaylight", &DayCycle::GetDaylight)
        .Property(
            "dayLength", [](const DayCycle& cycle) { return cycle.dayLength; },
            [](DayCycle& cycle, float seconds) {
                if (seconds < 0.0f)
                    throw std::invalid_argument("Day length cannot be negative");
                cycle.dayLength = seconds;
            })
        .Method("SetTimeOfDay", &DayCycle::SetTimeOfDay);
}

} // namespace

SPOT_GAME_MODULE(RegisterScriptBindings)
