#pragma once

#include "gameplay/menus/menu.h"

namespace Gameplay {

// Pauses the game, with resume, quit and an optional return to a menu scene. Its parts, such as the settings and
// bindings, are on its entity.
class PauseMenu : public Menu {
public:
    Spot::EntityId resumeButton = Spot::EntityId::Invalid;
    Spot::EntityId quitButton = Spot::EntityId::Invalid;
    Spot::EntityId mainMenuButton = Spot::EntityId::Invalid;
    Spot::AssetId mainMenuScene;

    PauseMenu();

protected:
    void Initialize() override;
    void AnimateControls(float blend) override;
    void Serialize(Spot::Archive& archive) override;
};

} // namespace Gameplay
