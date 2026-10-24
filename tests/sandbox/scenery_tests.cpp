#include "../registered_assets.h"
#include "day_cycle.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <vector>

TEST_CASE("The cooked playground renders its grass and trees up close", "[gpu][sandbox][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Tests::RegisteredAssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
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
    // The HUD, made as the game starts, would cover the views.
    scene->Update(0.0f);
    if (auto* game = scene->FindEntity("Game")) {
        game->ForEachChild([](Spot::Entity& child) { child.SetActive(false); });
    }
    auto& camera = scene->CreateEntity("Acceptance Camera");
    camera.AddComponent<Spot::Camera>().SetFarPlane(600.0f);
    auto* day = scene->FindComponent<DayCycle>();
    REQUIRE(day);
    auto target = device->CreateTexture(
        {.width = 960, .height = 540, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb});
    REQUIRE(renderer.IsValid());
    struct View {
        const char* name;
        Spot::Vector3 position;
        Spot::Vector3 target;
    };
    // The lawn at a player's feet, an oak on it, the pines on the hills and the hills from the valley.
    const std::array views{
        View{"scenery_grass.png", {0, 1.7f, 42}, {-1, 0, 38}},
        View{"scenery_oak.png", {3, 1.7f, 52}, {-6, 3.5f, 44}},
        View{"scenery_forest.png", {-52, 6, 52}, {-75, 12, 75}},
        View{"scenery_hills.png", {-40, 4, 30}, {-80, 10, 70}},
    };
    const std::filesystem::path output(SPOT_SANDBOX_CAPTURE_DIR);
    std::filesystem::create_directories(output);
    day->timeOfDay = 0.42f;
    for (const View& view : views) {
        camera.GetTransform().SetPosition(view.position);
        camera.GetTransform().SetRotation(Spot::Quaternion::LookRotation(view.target - view.position));
        scene->Update(0.0f);
        for (int frame = 0; frame < 2; ++frame) {
            auto* context = device->BeginOffscreenFrame(*target);
            REQUIRE(context);
            renderer.Render(*context, *scene, target.get());
            device->EndFrame();
        }
        auto pixels = device->DownloadTexture(*target);
        REQUIRE(pixels.size() == 960 * 540 * 4);
        // Most of each view is lit green: grass, leaves and needles.
        std::size_t green = 0;
        for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 4) {
            green +=
                pixels[pixel + 1] > pixels[pixel] && pixels[pixel + 1] > pixels[pixel + 2] && pixels[pixel + 1] > 25;
        }
        CAPTURE(view.name);
        CHECK(green > 960 * 540 / 4);
        CHECK(Spot::Image(960, 540, std::move(pixels)).SavePng(output / view.name));
    }
}
