#include "../registered_assets.h"
#include "day_cycle.h"

#include <spot.h>
#include <spot/scene/scripting/script_component.h>

#include <catch2/catch_test_macros.hpp>

#include <array>

TEST_CASE("Luau scripts change the rendered monument and daylight in the cooked sandbox", "[gpu][sandbox][luau]") {
    if (!Spot::GraphicsDevice::IsSupported())
        SKIP("No usable graphics device");
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Tests::RegisteredAssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
    Spot::InputState input;
    struct Restore {
        Spot::AssetManager* assets = Spot::Assets::GetManager();
        Spot::InputState* input = Spot::Input::GetState();
        ~Restore() {
            Spot::Detail::SetAssetManager(assets);
            Spot::Input::SetState(input);
        }
    } restore;
    Spot::Detail::SetAssetManager(&assets);
    Spot::Input::SetState(&input);
    Spot::Detail::LoadGameCode();
    auto settings = Spot::GameSettings::LoadFromFile(std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() /
                                                     Spot::GameSettings::FileName);
    REQUIRE(settings);
    auto scene = Spot::InstantiateScene(assets, settings->startupScene);
    REQUIRE(scene);
    scene->ForEachComponent<Spot::Camera>([](Spot::Camera& camera) { camera.SetEnabled(false); });
    scene->Update(0);
    scene->FindEntity("Game")->ForEachChild([](Spot::Entity& child) { child.SetActive(false); });
    auto& camera = scene->CreateEntity("Luau Capture Camera");
    camera.AddComponent<Spot::Camera>().SetFarPlane(600);
    camera.GetTransform().SetPosition({0, 1.7f, -1});
    camera.GetTransform().SetRotation(Spot::Quaternion::LookRotation({0, -0.08f, -1}));
    auto* cycle = scene->FindComponent<DayCycle>();
    REQUIRE(cycle);
    cycle->dayLength = 0;
    auto target = device->CreateTexture(
        {.width = 960, .height = 540, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb});
    REQUIRE(renderer.IsValid());
    const auto capture = [&](const char* name) {
        for (int frame = 0; frame < 2; ++frame) {
            auto* context = device->BeginOffscreenFrame(*target);
            REQUIRE(context);
            renderer.Render(*context, *scene, target.get());
            device->EndFrame();
        }
        auto pixels = device->DownloadTexture(*target);
        REQUIRE(pixels.size() == 960 * 540 * 4);
        const auto output = std::filesystem::path(SPOT_SANDBOX_CAPTURE_DIR);
        std::filesystem::create_directories(output);
        CHECK(Spot::Image(960, 540, pixels).SavePng(output / name));
        return pixels;
    };
    input.OnEvent(Spot::KeyPressedEvent{Spot::Key::K, false});
    scene->Update(1.0f / 60);
    input.BeginFrame();
    const auto day = capture("luau_monument_day.png");
    input.OnEvent(Spot::KeyPressedEvent{Spot::Key::N, false});
    scene->Update(1.0f / 60);
    input.BeginFrame();
    auto* neon = scene->FindEntity("Monument Sign")->FindChild("Neon")->GetComponent<Spot::MeshRenderer>();
    REQUIRE(neon);
    CHECK_FALSE(neon->IsEnabled());
    const auto off = capture("luau_monument_off.png");
    std::size_t changed = 0;
    for (std::size_t pixel = 0; pixel < day.size(); pixel += 4)
        changed += day[pixel] != off[pixel] || day[pixel + 1] != off[pixel + 1] || day[pixel + 2] != off[pixel + 2];
    CHECK(changed > 100);
    input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::N});
    scene->Update(1.0f / 60);
    input.BeginFrame();
    input.OnEvent(Spot::KeyPressedEvent{Spot::Key::N, false});
    input.OnEvent(Spot::KeyPressedEvent{Spot::Key::L, false});
    scene->Update(1.0f / 60);
    input.BeginFrame();
    CHECK(neon->IsEnabled());
    CHECK(cycle->GetDaylight() < 0.01f);
    const auto night = capture("luau_monument_night.png");
    CHECK(day != night);
    int scripts = 0;
    scene->ForEachComponent<Spot::ScriptComponent>([&](Spot::ScriptComponent& script) {
        ++scripts;
        CHECK_FALSE(script.IsFaulted());
    });
    CHECK(scripts == 3);
}
