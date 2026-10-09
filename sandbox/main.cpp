#include <spot.h>
#include <spot_tools.h>

#include <cstdlib>

// Runs the sandbox project, which the editor opens too, from its startup scene. Everything the game does is in the
// project's components.
int main() {
    const auto project = Spot::Project::Load(SPOT_SANDBOX_PROJECT);
    if (!project) {
        return EXIT_FAILURE;
    }
    // The sandbox imports the project's sources from the repository, into "imported" next to the executable, which
    // is all a shipped build would need.
    Spot::Application sandbox({.game = project->GetGameSettings(),
                               .window = {.title = "Spot Sandbox"},
                               .logFile = "sandbox.log",
                               .maxFrameRate = 60});
    Spot::AssetDatabase(project->GetAssetsDirectory(), Spot::GetExecutableDirectory() / "imported").Refresh();
    return sandbox.Run();
}
