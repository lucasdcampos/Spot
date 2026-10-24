// spot_player: the executable of games without code of their own. Shipped, it runs the game whose settings are beside
// it; the tools run it with --game, pointing at a folder with the settings of a project's game.
#include <spot.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <utility>

namespace {

int Run(Spot::ApplicationConfig config) {
    Spot::Application application(std::move(config));
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

} // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        return Run({});
    }
    if (argc != 3 || std::string_view(argv[1]) != "--game") {
        std::fprintf(stderr, "Usage: spot_player [--game <folder with %s>]\n", Spot::GameSettings::FileName.data());
        return EXIT_FAILURE;
    }
    // Arguments come in the encoding the OS uses for narrow paths, which this constructor expects.
    const std::filesystem::path folder = std::filesystem::absolute(std::filesystem::path(argv[2]));
    auto game = Spot::GameSettings::LoadFromFile(folder / Spot::GameSettings::FileName);
    if (!game) {
        std::fprintf(stderr, "There is no game in '%s'\n", Spot::PathToUtf8(folder).c_str());
        return EXIT_FAILURE;
    }
    Spot::ApplicationConfig config;
    // What the settings say is relative to their folder, as it is to the executable of a shipped game.
    config.assets.importDirectory = folder / Spot::Utf8ToPath(game->importDirectory);
    config.logFile = folder / "game.log";
    config.game = std::move(game);
    return Run(std::move(config));
}
