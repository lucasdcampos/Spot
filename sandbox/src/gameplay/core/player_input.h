#pragma once

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
};

} // namespace Gameplay
