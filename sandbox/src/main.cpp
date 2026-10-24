#include "smoke_test.h"

#include <spot.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>

// Runs the game from its startup scene, as the settings the tools write beside the executable say.
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--smoke-test") {
        return RunSandboxSmokeTest();
    }
    if (argc != 1) {
        std::fprintf(stderr, "Usage: Sandbox [--smoke-test]\n");
        return EXIT_FAILURE;
    }
    Spot::Application application;
    application.AddModule<Spot::UiModule>();
    application.AddModule<Spot::RenderModule>();
    application.AddModule<Spot::AudioModule>();
    application.AddModule<Spot::PhysicsModule>();
    application.AddModule<Spot::Physics2DModule>();
    application.AddModule<Spot::AnimationModule>();
    application.AddModule<Spot::ParticlesModule>();
    application.AddModule<Spot::ScriptingModule>();
    return application.Run();
}
