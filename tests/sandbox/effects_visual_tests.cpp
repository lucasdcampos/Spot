#include "day_cycle.h"
#include "explosion.h"
#include "flash.h"
#include "particle_effects.h"
#include "water.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

TEST_CASE("The cooked sandbox draws explosions, muzzle flashes and splashes", "[gpu][sandbox][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::AssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
    auto* previousAssets = Spot::Assets::GetManager();
    Spot::Detail::SetAssetManager(&assets);
    struct RestoreAssets {
        Spot::AssetManager* previous;
        ~RestoreAssets() { Spot::Detail::SetAssetManager(previous); }
    } restore{previousAssets};
    Spot::Detail::GameCode::Load();
    const auto settings = Spot::GameSettings::LoadFromFile(std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() /
                                                           Spot::GameSettings::FileName);
    REQUIRE(settings);
    auto scene = Spot::InstantiateScene(assets, settings->startupScene);
    REQUIRE(scene);
    scene->ForEachComponent<Spot::Camera>([](Spot::Camera& camera) { camera.SetEnabled(false); });
    scene->Update(0.0f);
    // The HUD would cover the views.
    if (auto* game = scene->FindEntity("Game")) {
        game->ForEachChild([](Spot::Entity& child) { child.SetActive(false); });
    }
    auto& camera = scene->CreateEntity("Acceptance Camera");
    camera.AddComponent<Spot::Camera>().SetFarPlane(600.0f);
    auto* day = scene->FindComponent<DayCycle>();
    REQUIRE(day);
    day->timeOfDay = 0.45f;
    day->dayLength = 0.0f;
    auto target = device->CreateTexture(
        {.width = 960, .height = 540, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb});
    REQUIRE(renderer.IsValid());
    const std::filesystem::path output(SPOT_SANDBOX_CAPTURE_DIR);
    std::filesystem::create_directories(output);
    const auto look = [&](Spot::Vector3 from, Spot::Vector3 at) {
        camera.GetTransform().SetPosition(from);
        camera.GetTransform().SetRotation(Spot::Quaternion::LookRotation(at - from));
    };
    const auto run = [&](float seconds) {
        for (float time = 0.0f; time < seconds; time += Spot::Scene::DefaultFixedTimeStep) {
            scene->Update(Spot::Scene::DefaultFixedTimeStep);
        }
    };
    const auto draw = [&](const std::string& name) {
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        renderer.Render(*context, *scene, target.get());
        device->EndFrame();
        auto pixels = device->DownloadTexture(*target);
        REQUIRE(pixels.size() == 960 * 540 * 4);
        CHECK(Spot::Image(960, 540, pixels).SavePng(output / name));
        return pixels;
    };
    const auto differs = [](const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
        std::size_t changed = 0;
        for (std::size_t i = 0; i < a.size(); i += 4) {
            changed += std::abs(int(a[i]) - int(b[i])) + std::abs(int(a[i + 1]) - int(b[i + 1])) > 40;
        }
        return changed;
    };

    // An explosion on the firing range's ground, as it unfolds.
    const auto ground = scene->GetPhysics().Raycast({40, 30, 6}, {0, -1, 0}, 60);
    REQUIRE(ground);
    const Spot::Vector3 blast = ground->point + Spot::Vector3{0, 0.5f, 0};
    look(blast + Spot::Vector3{-16, 4, 0}, blast + Spot::Vector3{0, 3, 0});
    run(0.1f);
    const auto before = draw("effects_before.png");
    Explosion::Explode(*scene, blast, 5.0f, 0.0f, 0.0f);
    const std::array<std::pair<float, const char*>, 4> stages{{{0.05f, "explosion_flash.png"},
                                                               {0.3f, "explosion_fireball.png"},
                                                               {1.0f, "explosion_smoke.png"},
                                                               {2.5f, "explosion_late.png"}}};
    float elapsed = 0.0f;
    for (const auto& [time, name] : stages) {
        run(time - elapsed);
        elapsed = time;
        const auto image = draw(name);
        CAPTURE(name);
        CHECK(differs(before, image) > 960 * 540 / 100);
    }
    run(5.0f);

    // A shot's muzzle flash, close up from the side.
    const Spot::Vector3 muzzle = blast + Spot::Vector3{-10, 1, 0};
    look(muzzle + Spot::Vector3{0, 0.1f, 1.2f}, muzzle + Spot::Vector3{0.3f, 0, 0});
    run(0.05f);
    const auto noFlash = draw("muzzle_before.png");
    Flash::Spawn(*scene, muzzle, {1.0f, 0.75f, 0.4f, 1.0f}, 5.0f, 12.0f, 0.05f, 0.3f);
    ParticleEffects::Muzzle(*scene, muzzle, {1, 0, 0}, 1.6f);
    run(0.02f);
    CHECK(differs(noFlash, draw("muzzle_flash.png")) > 0);
    run(0.3f);
    draw("muzzle_smoke.png");

    // The pond: a shot, a crate falling in and a rocket on the water.
    Water* water = Water::FindAt(*scene, {30, 0, 40});
    REQUIRE(water);
    const Spot::Vector3 middle{30, water->GetHeight(), 40};
    look(middle + Spot::Vector3{-12, 3, -9}, middle + Spot::Vector3{0, 1, 0});
    const auto calm = draw("pond_calm.png");
    water->Splash(middle + Spot::Vector3{-3, 0, -2}, 0.25f);
    water->Splash(middle + Spot::Vector3{2, 0, -3}, 1.7f);
    run(0.15f);
    CHECK(differs(calm, draw("pond_splash.png")) > 0);
    run(0.8f);
    draw("pond_ripples.png");
    Explosion::Explode(*scene, middle + Spot::Vector3{1, 0, 3}, 5.0f, 0.0f, 0.0f);
    run(0.4f);
    CHECK(differs(calm, draw("pond_plume.png")) > 960 * 540 / 100);
    run(1.2f);
    draw("pond_plume_falling.png");
}
