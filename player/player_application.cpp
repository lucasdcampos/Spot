#include "player_application.h"

namespace Spot::Player {

Logger& GetPlayerLogger() {
    static Logger logger("Player");
    return logger;
}

namespace {

ApplicationConfig MakeConfig(const Project& project, const PlayerOptions& options) {
    // The sources are imported first, as the editor would, so the game runs what was saved last.
    return {
        .game = project.GetGameSettings(),
        .graphics = {.enabled = options.graphics},
        .assets = {.importDirectory = project.GetImportDirectory(), .sourceDirectory = project.GetAssetsDirectory()},
    };
}

} // namespace

PlayerApplication::PlayerApplication(const Project& project, PlayerOptions options)
    : Application(MakeConfig(project, options)), m_project(project), m_options(options) {
}

void PlayerApplication::OnStart() {
    // The application loads the game's startup scene next.
    GetPlayerLogger().Info("Running '{}'", m_project.GetSettings().name);
}

void PlayerApplication::OnUpdate(float) {
    if (m_options.frames > 0 && ++m_frame >= m_options.frames) {
        Quit();
    }
}

} // namespace Spot::Player
