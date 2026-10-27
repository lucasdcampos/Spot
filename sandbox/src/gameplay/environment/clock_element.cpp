#include "gameplay/environment/clock_element.h"

#include "gameplay/environment/day_cycle.h"

#include <format>

namespace Gameplay {

SPOT_COMPONENT(ClockElement);

void ClockElement::Refresh(const HudContext& /*context*/) {
    const DayCycle* day = GetScene().FindComponent<DayCycle>();
    const int minutes = day ? static_cast<int>(day->timeOfDay * 24.0f * 60.0f) % (24 * 60) : 0;
    SetText(text, std::format("{:02}:{:02}", minutes / 60, minutes % 60));
    // Day from 6:00 to 18:00.
    if (auto* image = Get<Spot::UiImage>(icon)) {
        image->texture = minutes >= 6 * 60 && minutes < 18 * 60 ? sunIcon : moonIcon;
    }
}

void ClockElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("text", text);
    archive.Field("icon", icon);
    archive.Field("sunIcon", sunIcon);
    archive.Field("moonIcon", moonIcon);
}

} // namespace Gameplay
