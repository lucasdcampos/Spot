#pragma once

namespace Spot {
class Archive;
} // namespace Spot

namespace Gameplay {

class PlayerController;

// Translates a local player's actions into intents for a part of its pawn. The controller calls these listeners;
// simulation and item behavior use the resulting intents, including those supplied by AI or tests.
class PlayerInputListener {
public:
    virtual ~PlayerInputListener() = default;
    // Movement intents must be ready before this frame's first physics step; item use follows view updates.
    virtual bool IsInputBeforeFixedSteps() const { return false; }
    virtual void OnPlayerInput(PlayerController& player) = 0;
    virtual void OnPlayerUnpossess() {}
    // The intent it made of its player's input, for a server to apply to the same part of its copy of the pawn, as
    // when the player plays on another machine: saving writes the intent, loading applies it as the part's own setter
    // would. Each call is one frame's intent, so presses that last a frame are not lost.
    virtual void SerializeIntent(Spot::Archive& /*archive*/) {}
};

} // namespace Gameplay
