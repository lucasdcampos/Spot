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

} // namespace Gameplay
