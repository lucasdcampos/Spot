#pragma once

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

// Points a character's aim constraints where its pawn looks, up and down as well as around: one that turns the head,
// and one that turns the body to aim what it holds, which WorldModel turns on while an item is shown.
class CharacterAim : public Spot::Component {
public:
    Spot::ComponentRef<Spot::AimIK> look;
    Spot::ComponentRef<Spot::AimIK> aim;
    // How far ahead of the eyes the constraints aim, so what is held and the eyes point at the same place.
    float distance = 20.0f;
    // States in which the head looks where the animation has it.
    std::vector<std::string> hiddenWhen = {"Dead", "Seated"};

protected:
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;
};

} // namespace Gameplay
