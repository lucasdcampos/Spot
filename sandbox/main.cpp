#include <spot.h>
#include <spot_tools.h>

#include <cstdlib>

// Defined by SPOT_GAME_MODULE() in src/game.cpp: it registers the sandbox's components, as it does when the editor
// loads the same code as a module.
extern "C" void SpotLoadGameModule();

// Runs the sandbox project, which the editor opens too, from its startup scene. Everything the game does is in the
// project's components.
int main() {
    const auto project = Spot::Project::Load(SPOT_SANDBOX_PROJECT);
    if (!project) {
        return EXIT_FAILURE;
    }
    SpotLoadGameModule();
    // In development the sandbox imports the project's sources from the repository, into "imported" next to the
    // executable, which is all a shipped build would need.
    Spot::Application sandbox({.game = project->GetGameSettings(),
                               .window = {.title = "Spot Sandbox"},
                               .assets = {.sourceDirectory = project->GetAssetsDirectory()},
                               .logFile = "sandbox.log",
                               .maxFrameRate = 60});
    return sandbox.Run();
}
