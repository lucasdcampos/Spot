#include "gameplay/game/death_screen_element.h"

#include "gameplay/core/player_controller.h"
#include "gameplay/game/game_mode.h"

#include <cmath>
#include <format>

namespace Gameplay {

SPOT_COMPONENT(DeathScreenElement);

void DeathScreenElement::Refresh(const HudContext& context) {
    const auto* mode = GameMode::Get(GetScene());
    const int seconds =
        static_cast<int>(std::ceil(mode && context.player ? mode->GetRespawnTimeLeft(*context.player) : 0.0f));
    SetText(timer, std::vformat(timerText, std::make_format_args(seconds)));
}

void DeathScreenElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("timer", timer);
    archive.Field("timerText", timerText, {.tooltip = "{} stands for the seconds left"});
}

} // namespace Gameplay
