#include "day_cycle.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>

TEST_CASE("The cooked playground renders the workshop, glass and night lighting", "[gpu][sandbox][offscreen]") {
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
    auto scene = assets.InstantiateScene(settings->startupScene);
    REQUIRE(scene);
    REQUIRE(scene->FindEntity("Workshop"));
    scene->ForEachComponent<Spot::Camera>([](Spot::Camera& camera) { camera.SetEnabled(false); });
    auto& camera = scene->CreateEntity("Acceptance Camera");
    camera.AddComponent<Spot::Camera>();
    auto* day = scene->FindComponent<DayCycle>();
    REQUIRE(day);
    auto target = device->CreateTexture(
        {.width = 960, .height = 540, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb});
    REQUIRE(renderer.IsValid());
    const std::array views{Spot::Vector3{0, 8, 16}, Spot::Vector3{12, 1.6f, -8}, Spot::Vector3{19, 1.8f, -12}};
    std::array<std::vector<std::uint8_t>, 3> images;
    const std::array names{"playground_day.png", "workshop_night.png", "workshop_glass.png"};
    const std::filesystem::path output(SPOT_SANDBOX_CAPTURE_DIR);
    std::filesystem::create_directories(output);
    for (std::size_t i = 0; i < views.size(); ++i) {
        camera.GetTransform().SetPosition(views[i]);
        camera.GetTransform().SetRotation(Spot::Quaternion::LookRotation(Spot::Vector3{10, 1.5f, -12} - views[i]));
        day->timeOfDay = i == 1 ? 0 : .5f;
        scene->Update(0);
        std::array scenes{scene.get()};
        for (int frame = 0; frame < 2; ++frame) {
            auto* context = device->BeginOffscreenFrame(*target);
            REQUIRE(context);
            renderer.Render(*context, scenes, target.get());
            device->EndFrame();
        }
        images[i] = device->DownloadTexture(*target);
        REQUIRE(images[i].size() == 960 * 540 * 4);
        std::size_t visible = 0;
        for (std::size_t pixel = 0; pixel < images[i].size(); pixel += 4) {
            visible += images[i][pixel] > 30 || images[i][pixel + 1] > 30 || images[i][pixel + 2] > 30;
        }
        CHECK(visible > 960 * 540 / 8);
        CHECK(Spot::Image(960, 540, images[i]).SavePng(output / names[i]));
    }
    CHECK(images[0] != images[1]);
    scene->ForEachComponent<Spot::MeshRenderer>([](Spot::MeshRenderer& mesh) {
        if (mesh.GetEntity().GetName() == "Workshop Window") {
            mesh.SetEnabled(false);
        }
    });
    auto* context = device->BeginOffscreenFrame(*target);
    REQUIRE(context);
    const std::array scenes{scene.get()};
    renderer.Render(*context, scenes, target.get());
    device->EndFrame();
    CHECK(device->DownloadTexture(*target) != images[2]);
}
