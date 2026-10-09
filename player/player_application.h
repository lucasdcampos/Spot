#pragma once

#include <spot.h>
#include <spot_tools.h>

namespace Spot::Player {

// The player's own messages, shown as "Player".
Logger& GetPlayerLogger();

struct PlayerOptions {
    // Without graphics, nothing is drawn and no GPU is needed, as for tests.
    bool graphics = true;
    // Quits after this many frames when above 0, as tests do.
    int frames = 0;
};

// Runs a project as its game, from its startup scene. The game module with the project's components must be loaded
// already.
class PlayerApplication final : public Application {
public:
    PlayerApplication(const Project& project, PlayerOptions options);

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;

private:
    const Project& m_project;
    PlayerOptions m_options;
    int m_frame = 0;
};

} // namespace Spot::Player
