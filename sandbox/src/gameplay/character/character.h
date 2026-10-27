#pragma once

#include "gameplay/core/pawn.h"

#include <spot.h>

#include <string>

namespace Gameplay {

// A pawn on foot: its CharacterMovement walks it, its FirstPersonView looks for it and gives it its view point, and
// it is "OnFoot" for whatever shows what fits. Landings are told by OnLanded, which its FallDamage takes; hurts kick
// its view and rumble the gamepad. Getting out of a vehicle places it facing the vehicle's way, moving as it moved.
class Character : public Pawn {
public:
    // Meters per second it landed at, falling.
    Spot::Signal<float> OnLanded;

    ViewPoint GetViewPoint() const override;
    PawnMotion GetMotion() const override;
    void Place(Spot::Vector3 position, Spot::Vector3 forward, Spot::Vector3 velocity) override;
    void ResetMotion() override;

protected:
    void OnStart() override;

private:
    Spot::SignalConnection m_damaged;
    Spot::SignalConnection m_landed;
};

} // namespace Gameplay
