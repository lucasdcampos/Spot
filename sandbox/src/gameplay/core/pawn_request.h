#pragma once

#include <spot.h>

#include <string>
#include <string_view>

namespace Gameplay {

// A discrete action on the pawn a player possesses. NetworkPlayers checks possession before delivering it; the
// part handling the action still checks whether it is allowed, as an interactor checking reach on the server.
struct PawnRequest {
    static constexpr std::string_view NetName = "Gameplay.PawnRequest";
    std::string name;

    void Serialize(Spot::Archive& archive) { archive.Field("name", name); }
};

// To the client deciding a pawn: the server teleported it (Pawn::Teleport()), which that client does too.
struct PawnPlaced {
    static constexpr std::string_view NetName = "Gameplay.PawnPlaced";
    Spot::Vector3 position;
    Spot::Vector3 forward;
    Spot::Vector3 velocity;

    void Serialize(Spot::Archive& archive) {
        archive.Field("position", position);
        archive.Field("forward", forward);
        archive.Field("velocity", velocity);
    }
};

} // namespace Gameplay
