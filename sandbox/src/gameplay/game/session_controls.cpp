#include "gameplay/game/session_controls.h"

#include "gameplay/game/game_mode.h"
#include "gameplay/game/map_info.h"
#include "gameplay/menus/menu.h"
#include "gameplay/menus/pause_menu.h"
#include "gameplay/persistence/world_save.h"

namespace Gameplay {

SPOT_COMPONENT(SessionControls);

void SessionControls::OnStart() {
    Spot::Input::SetCursorLocked(true);
}

void SessionControls::PlayClick() const {
    if (auto* device = Spot::Audio::GetDevice(); device && clickSound) {
        device->Play(clickSound.GetShared(), {.volume = 0.5f, .bus = Spot::Audio::FindBus("Interface")});
    }
}

void SessionControls::OnUpdate(float /*deltaTime*/) {
    // The world is the server's: a client neither saves it, nor loads it, nor restarts it.
    const bool server = Spot::Network::IsServer(GetScene());
    const bool save = server && Spot::Input::IsActionPressed(saveAction);
    const bool load = server && Spot::Input::IsActionPressed(loadAction);
    if (save || load) {
        const auto directory = !m_saveDirectory.empty() ? m_saveDirectory
                               : GetScene().IsLoaded()  ? Spot::Application::Get().GetUserDataDirectory()
                                                        : std::filesystem::path{};
        if (!directory.empty()) {
            const MapInfo* map = MapInfo::Get(GetScene());
            const std::string checkpoint = map ? map->checkpoint : "world.json";
            const auto result = save ? WorldSave::Save(GetScene(), directory / checkpoint)
                                     : WorldSave::Load(GetScene(), directory / checkpoint);
            if (result) {
                Spot::Log::Info("World {}", save ? "saved" : "loaded");
            } else {
                Spot::Log::Warning("World {} failed: {}", save ? "save" : "load", result.message);
            }
        }
    }
    if (Spot::Input::IsActionPressed(pauseAction) && !GetScene().FindComponent<PauseMenu>()) {
        PlayClick();
        if (Spot::Input::IsCursorLocked()) {
            Spot::Input::SetCursorLocked(false);
        } else if (GetScene().IsLoaded()) {
            Spot::Application::Get().QuitGame();
        }
    }
    if (server && Spot::Input::IsActionPressed(restartAction)) {
        if (auto* mode = GameMode::Get(GetScene())) {
            mode->Restart();
        }
    }
}

void SessionControls::OnLateUpdate(float /*deltaTime*/) {
    if (Spot::Input::IsActionPressed(captureAction) && !Spot::Input::IsCursorLocked() && !Menu::GetOpen(GetScene())) {
        PlayClick();
        Spot::Input::SetCursorLocked(true);
    }
}

void SessionControls::Serialize(Spot::Archive& archive) {
    archive.Field("saveAction", saveAction);
    archive.Field("loadAction", loadAction);
    archive.Field("pauseAction", pauseAction);
    archive.Field("restartAction", restartAction);
    archive.Field("captureAction", captureAction);
    archive.Field("clickSound", clickSound);
}

} // namespace Gameplay
