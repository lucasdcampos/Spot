#pragma once

#include <string_view>

#include <spot/core/math/vector.h>

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
    // A discrete action requested by the player, delivered only on the server (or while playing alone). The listener
    // validates what the action needs before changing the world; repeated movement intents never repeat it.
    virtual void OnPlayerRequest(std::string_view /*request*/) {}
    // The intent it made of its player's input, for a server to apply to the same part of its copy of the pawn, as
    // when the player plays on another machine: saving writes the intent, loading applies it as the part's own setter
    // would. Each call is one frame's intent, so presses that last a frame are not lost.
    virtual void SerializeIntent(Spot::Archive& /*archive*/) {}
    // Whether its intent also goes with each fixed step of a predicted pawn (see PredictedPart), as the movement and
    // the view it goes along do, rather than only with each frame.
    virtual bool IsIntentPerStep() const { return false; }
};

// A part of a pawn its intent moves a fixed step at a time, as a character's movement. Where the pawn's network object
// is predicted (Spot::NetworkObject::predicted), its player's machine steps it at once, the server steps it by the
// same intents, one step for each that machine took, and sends back the state each left, from which that machine
// corrects itself, stepping again what the server had not seen yet. Its intent goes with each step, not each frame.
class PredictedPart {
public:
    virtual ~PredictedPart() = default;
    // One fixed step with the intent it has. Replaying steps already shown, it changes its state only, without
    // telling what happened again, as a landing.
    virtual void Step(float fixedDeltaTime, bool replaying) = 0;
    // The state a step leaves, intent included, apart from where its entity is: saving writes it, loading puts the
    // part back in it as it was, without acting on what changed.
    virtual void SerializeStepState(Spot::Archive& archive) = 0;
    // After a correction moved its entity by the offset, for what is shown of it to ease back instead of jumping.
    virtual void OnCorrected(Spot::Vector3 /*offset*/) {}
};

} // namespace Gameplay
