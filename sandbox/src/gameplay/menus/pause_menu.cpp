#include "gameplay/menus/pause_menu.h"

namespace Gameplay {

SPOT_COMPONENT(PauseMenu);

PauseMenu::PauseMenu() {
    toggleAction = "Pause";
    pausesGame = true;
}

void PauseMenu::Initialize() {
    if (firstFocus == Spot::EntityId::Invalid) {
        firstFocus = resumeButton;
    }
    ConnectButton(resumeButton, [this] { Close(); });
    ConnectButton(quitButton, [this] {
        Close();
        if (GetScene().IsLoaded()) {
            Spot::Application::Get().QuitGame();
        }
    });
}

void PauseMenu::AnimateControls(float blend) {
    StyleControl(quitButton, false, style.textSecondary, blend);
}

void PauseMenu::Serialize(Spot::Archive& archive) {
    Menu::Serialize(archive);
    archive.Field("resumeButton", resumeButton);
    archive.Field("quitButton", quitButton);
}

} // namespace Gameplay
