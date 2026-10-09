#include <spot.h>

#include <optional>

// Defined by SPOT_GAME_MODULE() in src/game.cpp: it registers the sandbox's components, as it does when the editor
// loads the same code as a module.
extern "C" void SpotLoadGameModule();

// Runs the sandbox project, which the editor opens too, as the player would: its startup scene, which the application
// draws. Everything the game does is in the project's components.
class Sandbox final : public Spot::Application {
public:
    // In development the sandbox imports the project's sources from the repository, into "imported" next to the
    // executable, which is all a shipped build would need.
    explicit Sandbox(const Spot::Project& project)
        : Application({.window = {.title = "Spot Sandbox"},
                       .assets = {.sourceDirectory = project.GetAssetsDirectory()},
                       .logFile = "sandbox.log",
                       .maxFrameRate = 60}),
          m_project(project) {}

protected:
    void OnStart() override {
        Spot::Log::Info("Spot {}", Spot::GetVersionString());
        auto scene = GetAssets().InstantiateScene(m_project.GetSettings().startupScene);
        if (!scene) {
            Spot::Log::Error("The sandbox's startup scene could not be loaded");
            Quit();
            return;
        }
        LoadScene(std::move(scene));
    }

private:
    const Spot::Project& m_project;
};

int main() {
    const std::optional<Spot::Project> project = Spot::Project::Load(SPOT_SANDBOX_PROJECT);
    if (!project) {
        return 1;
    }
    SpotLoadGameModule();
    Sandbox sandbox(*project);
    return sandbox.Run();
}
