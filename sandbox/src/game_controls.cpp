#include "game_controls.h"

SPOT_COMPONENT(GameControls);

void GameControls::OnStart() {
    Spot::Input::SetCursorLocked(true);
}

void GameControls::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;

    if (Input::IsKeyPressed(Spot::Key::Escape)) {
        if (Input::IsCursorLocked()) {
            Input::SetCursorLocked(false);
        } else {
            GetScene().GetApplication().QuitGame();
        }
    }
    if (Input::IsKeyPressed(Spot::Key::R) && restartScene.IsValid()) {
        if (auto scene = Spot::Assets::GetManager()->InstantiateScene(restartScene)) {
            GetScene().GetApplication().LoadScene(std::move(scene));
        }
    }
}

void GameControls::OnLateUpdate(float /*deltaTime*/) {
    if (Spot::Input::IsMouseButtonPressed(Spot::MouseButton::Left) && !Spot::Input::IsCursorLocked()) {
        Spot::Input::SetCursorLocked(true);
    }
}

void GameControls::Serialize(Spot::Archive& archive) {
    archive.Field("restartScene", restartScene, {.tooltip = "The scene R loads", .assetType = Spot::AssetType::Scene});
}
