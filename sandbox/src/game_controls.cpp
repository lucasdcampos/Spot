#include "game_controls.h"

#include "hud.h"
#include "sound_bank.h"
#include "world_save.h"

SPOT_COMPONENT(GameControls);

void GameControls::OnStart() {
    Spot::Input::SetCursorLocked(true);
    if (GetScene().IsLoaded()) {
        if (const auto* hud = GetScene().FindComponent<Hud>()) {
            GetScene().GetApplication().SetDeveloperFont(hud->font);
        }
    }
}

void GameControls::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;

    const bool save = Input::IsActionPressed("Quick Save");
    const bool load = Input::IsActionPressed("Quick Load");
    if (save || load) {
        const auto directory = !m_saveDirectory.empty() ? m_saveDirectory
                               : GetScene().IsLoaded()  ? GetScene().GetApplication().GetUserDataDirectory()
                                                        : std::filesystem::path{};
        if (!directory.empty()) {
            const auto result = save ? WorldSave::Save(GetScene(), directory / "world.json")
                                     : WorldSave::Load(GetScene(), directory / "world.json");
            if (result) {
                Spot::Log::Info("World {}", save ? "saved" : "loaded");
            } else {
                Spot::Log::Warning("World {} failed: {}", save ? "save" : "load", result.message);
            }
        }
    }

    if (Input::IsActionPressed("Pause")) {
        SoundBank::PlayClick(GetScene());
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
        SoundBank::PlayClick(GetScene());
        Spot::Input::SetCursorLocked(true);
    }
}

void GameControls::Serialize(Spot::Archive& archive) {
    archive.Field("restartScene", restartScene,
                  {.tooltip = "The scene the Restart action loads", .assetType = Spot::AssetType::Scene});
}
