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

// A predicting player's latest fixed steps of intents, oldest first, the newest being step.
struct StepsMessage {
    static constexpr std::string_view NetName = "Gameplay.Steps";
    std::uint32_t step = 0;
    std::vector<std::string> steps;

    void Serialize(Spot::Archive& archive) {
        archive.Field("step", step);
        archive.Field("steps", steps);
    }
};

// To a predicting player: where the server's pawn was after that player's step, and the state of its predicted parts.
struct StepStateMessage {
    static constexpr std::string_view NetName = "Gameplay.StepState";
    std::uint32_t step = 0;
    Spot::Vector3 position;
    std::string state;

    void Serialize(Spot::Archive& archive) {
        archive.Field("step", step);
        archive.Field("position", position);
        archive.Field("state", state);
    }
};

} // namespace Gameplay
