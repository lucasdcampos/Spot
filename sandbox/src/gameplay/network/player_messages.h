#pragma once

#include <spot.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Gameplay {

// What players' machines and the server tell each other about the pawns they play as (see NetworkPlayers).

// A player's latest frames of intents (PawnIntents), oldest first, the newest being frame.
struct IntentsMessage {
    static constexpr std::string_view NetName = "Gameplay.Intents";
    std::uint32_t frame = 0;
    std::vector<std::string> frames;

    void Serialize(Spot::Archive& archive) {
        archive.Field("frame", frame);
        archive.Field("frames", frames);
    }
};

} // namespace Gameplay
