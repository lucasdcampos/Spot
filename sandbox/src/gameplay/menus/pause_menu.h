#pragma once

#include "gameplay/menus/menu.h"

namespace Gameplay {

// Pauses the game over it, with a button to resume and one to quit; its parts, such as the settings and the bindings,
// are on its entity.
class PauseMenu : public Menu {
public:
    Spot::EntityId resumeButton = Spot::EntityId::Invalid;
    Spot::EntityId quitButton = Spot::EntityId::Invalid;

    PauseMenu();

protected:
    void Initialize() override;
    void AnimateControls(float blend) override;
    void Serialize(Spot::Archive& archive) override;
};

} // namespace Gameplay
