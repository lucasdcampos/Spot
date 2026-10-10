#include "game_controls.h"

SPOT_COMPONENT(GameControls);

void GameControls::OnStart() {
    Spot::Input::SetCursorLocked(true);
}

void GameControls::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;

    if (Input::IsActionPressed("Pause")) {
        if (Input::IsCursorLocked()) {
            Input::SetCursorLocked(false);
        } else {
            GetScene().GetApplication().QuitGame();
        }
    }
    if (Input::IsActionPressed("Restart") && restartScene.IsValid()) {
        if (auto scene = Spot::Assets::GetManager()->InstantiateScene(restartScene)) {
            GetScene().GetApplication().LoadScene(std::move(scene));
        }
    }
}

void GameControls::OnLateUpdate(float /*deltaTime*/) {
    if (Spot::Input::IsActionPressed("Fire") && !Spot::Input::IsCursorLocked()) {
        Spot::Input::SetCursorLocked(true);
    }
}

void GameControls::Serialize(Spot::Archive& archive) {
    archive.Field("restartScene", restartScene,
                  {.tooltip = "The scene the Restart action loads", .assetType = Spot::AssetType::Scene});
}
