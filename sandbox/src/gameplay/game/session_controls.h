#pragma once

#include <spot.h>

#include <filesystem>
#include <string>

namespace Gameplay {

// Cursor capture and the session's shortcuts, independent of the game's rules and pawn kinds. A PauseMenu handles
// Pause when present; otherwise it releases the cursor, then quits. The capture click runs after gameplay updates.
class SessionControls : public Spot::Component {
public:
    std::string saveAction = "Quick Save";
    std::string loadAction = "Quick Load";
    std::string pauseAction = "Pause";
    std::string restartAction = "Restart";
    std::string captureAction = "Fire";
    Spot::AssetRef<Spot::Sound> clickSound;

    void SetSaveDirectory(std::filesystem::path directory) { m_saveDirectory = std::move(directory); }

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    void PlayClick() const;
    std::filesystem::path m_saveDirectory;
};

} // namespace Gameplay
