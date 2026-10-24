// The data-driven example, run as the player runs it: from the game.json its cook wrote, with the player's modules.
#include <spot.h>
#include <spot/scripting.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <utility>

namespace {

class DataDrivenRun final : public Spot::Application {
public:
    explicit DataDrivenRun(Spot::ApplicationConfig config) : Application(std::move(config)) {
        AddModule<Spot::UiModule>();
        AddModule<Spot::RenderModule>();
        AddModule<Spot::AudioModule>(Spot::AudioConfig{.enabled = false});
        AddModule<Spot::PhysicsModule>();
        AddModule<Spot::Physics2DModule>();
        AddModule<Spot::AnimationModule>();
        AddModule<Spot::ParticlesModule>();
        AddModule<Spot::ScriptingModule>();
    }

    bool hadScene = false;
    float cubeTurn = 0.0f;

protected:
    void OnUpdate(float) override {
        if (++m_frame < 30) {
            return;
        }
        if (auto* scene = GetScene()) {
            hadScene = true;
            if (auto* cube = scene->FindEntity("Cube")) {
                cubeTurn = std::abs(cube->GetTransform().GetRotation().y);
            }
        }
        Quit();
    }

private:
    int m_frame = 0;
};

} // namespace

TEST_CASE("The data-driven example runs its scene and its script", "[gpu][examples]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No GPU backend is available on this machine");
    }
    const std::filesystem::path folder = SPOT_DATA_DRIVEN_EXAMPLE_DIR;
    auto game = Spot::GameSettings::LoadFromFile(folder / Spot::GameSettings::FileName);
    REQUIRE(game);
    Spot::ApplicationConfig config;
    config.assets.importDirectory = folder / Spot::Utf8ToPath(game->importDirectory);
    config.game = std::move(game);
    config.window = {.title = "Data-driven example", .width = 320, .height = 180};
    config.userDataDirectory = folder / "user";
    DataDrivenRun run(std::move(config));
    REQUIRE(run.Run() == 0);
    CHECK(run.hadScene);
    // The script turned the cube around the vertical axis.
    CHECK(run.cubeTurn > 0.01f);
}
