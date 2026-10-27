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
    ConnectButton(mainMenuButton, [this] {
        if (GetScene().IsLoaded()) {
            auto& application = Spot::Application::Get();
            if (auto scene = Spot::InstantiateScene(application.GetAssets(), mainMenuScene)) {
                Close();
                application.LoadScene(std::move(scene));
            }
        }
    });
    ConnectButton(quitButton, [this] {
        Close();
        if (GetScene().IsLoaded()) {
            Spot::Application::Get().QuitGame();
        }
    });
}

void PauseMenu::AnimateControls(float blend) {
    StyleControl(mainMenuButton, false, style.textSecondary, blend);
    StyleControl(quitButton, false, style.textSecondary, blend);
}

void PauseMenu::Serialize(Spot::Archive& archive) {
    Menu::Serialize(archive);
    archive.Field("resumeButton", resumeButton);
    archive.Field("quitButton", quitButton);
    archive.Field("mainMenuButton", mainMenuButton);
    archive.Field("mainMenuScene", mainMenuScene, {.assetType = Spot::AssetType::Scene});
}

} // namespace Gameplay
