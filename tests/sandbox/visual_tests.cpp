#include "day_cycle.h"
#include "player_controller.h"
#include "view_model.h"
#include "weapon.h"
#include "weapon_holder.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <vector>

TEST_CASE("The cooked playground renders its yards by day, at sunset and at night", "[gpu][sandbox][offscreen]") {
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
        float time;
    };
    const std::array views{
        View{"playground_day.png", {0, 7, 30}, {0, 2, -12}, 0.42f},
        View{"workshop_exterior.png", {6, 3.5f, -10}, {19, 3.5f, -30}, 0.6f},
        View{"workshop_night.png", {19, 2, -29}, {21, 1.5f, -42}, 0.0f},
        View{"proving_yard.png", {-20, 7, 8}, {-38, 3, -10}, 0.45f},
        View{"firing_range.png", {25.5f, 2.0f, 0}, {46, 1, 0}, 0.5f},
        View{"container_yard.png", {-26, 4, 27}, {-42, 2, 40}, 0.36f},
        View{"pond_sunset.png", {19, 3, 27}, {34, 0, 44}, 0.71f},
        View{"playground_night.png", {0, 9, 34}, {0, 2, -12}, 0.95f},
    };
    const std::filesystem::path output(SPOT_SANDBOX_CAPTURE_DIR);
    std::filesystem::create_directories(output);
    std::vector<std::vector<std::uint8_t>> images;
    for (const View& view : views) {
        camera.GetTransform().SetPosition(view.position);
        camera.GetTransform().SetRotation(Spot::Quaternion::LookRotation(view.target - view.position));
        day->timeOfDay = view.time;
        // Long enough for the night lights to come on.
        scene->Update(0.0f);
        scene->Update(0.0f);
        std::array scenes{scene.get()};
        for (int frame = 0; frame < 2; ++frame) {
            auto* context = device->BeginOffscreenFrame(*target);
            REQUIRE(context);
            renderer.Render(*context, scenes, target.get());
            device->EndFrame();
        }
        auto pixels = device->DownloadTexture(*target);
        REQUIRE(pixels.size() == 960 * 540 * 4);
        std::size_t visible = 0;
        for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 4) {
            visible += pixels[pixel] > 30 || pixels[pixel + 1] > 30 || pixels[pixel + 2] > 30;
        }
        CAPTURE(view.name);
        CHECK(visible > 960 * 540 / 8);
        CHECK(Spot::Image(960, 540, pixels).SavePng(output / view.name));
        images.push_back(std::move(pixels));
    }
    const bool nightDiffers = images[0] != images[7];
    CHECK(nightDiffers);
    // The workshop's windows are glass: hiding them changes what is seen through them. Disabled components are
    // left out of ForEachComponent(), so the windows are kept to enable them again.
    std::vector<Spot::MeshRenderer*> windows;
    scene->ForEachComponent<Spot::MeshRenderer>([&](Spot::MeshRenderer& mesh) {
        if (mesh.GetEntity().GetName() == "Workshop Window") {
            windows.push_back(&mesh);
            mesh.SetEnabled(false);
        }
    });
    REQUIRE(windows.size() == 5);
    camera.GetTransform().SetPosition({33, 2.2f, -37});
    camera.GetTransform().SetRotation(Spot::Quaternion::LookRotation(Spot::Vector3{-1, 0, 0}));
    day->timeOfDay = 0.5f;
    scene->Update(0.0f);
    const std::array scenes{scene.get()};
    auto* context = device->BeginOffscreenFrame(*target);
    REQUIRE(context);
    renderer.Render(*context, scenes, target.get());
    device->EndFrame();
    const auto withoutGlass = device->DownloadTexture(*target);
    for (auto* window : windows) {
        window->SetEnabled(true);
    }
    scene->Update(0.0f);
    context = device->BeginOffscreenFrame(*target);
    REQUIRE(context);
    renderer.Render(*context, scenes, target.get());
    device->EndFrame();
    const auto withGlass = device->DownloadTexture(*target);
    CHECK(Spot::Image(960, 540, withGlass).SavePng(output / "workshop_glass.png"));
    const bool glassShows = withGlass != withoutGlass;
    CHECK(glassShows);
}

TEST_CASE("The cooked sandbox renders held items, recoil, sights and reloads", "[gpu][sandbox][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::AssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
    Spot::InputState input;
    Spot::InputActionMap actions;
    struct Restore {
        Spot::AssetManager* assets = Spot::Assets::GetManager();
        Spot::InputState* input = Spot::Input::GetState();
        Spot::InputActionMap* actions = Spot::Input::GetActionMap();
        ~Restore() {
            Spot::Detail::SetAssetManager(assets);
            Spot::Input::SetState(input);
            Spot::Input::SetActionMap(actions);
        }
    } restore;
    Spot::Detail::SetAssetManager(&assets);
    Spot::Input::SetState(&input);
    Spot::Input::SetActionMap(&actions);
    Spot::Detail::GameCode::Load();
    const auto settings = Spot::GameSettings::LoadFromFile(std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() /
                                                           Spot::GameSettings::FileName);
    REQUIRE(settings);
    settings->sections.input.Apply(actions);
    auto scene = assets.InstantiateScene(settings->startupScene);
    REQUIRE(scene);
    auto step = [&] {
        actions.Update(input);
        scene->Update(Spot::Scene::DefaultFixedTimeStep);
        input.BeginFrame();
    };
    for (int i = 0; i < 90; ++i) {
        step();
    }
    auto* holder = scene->FindComponent<WeaponHolder>();
    auto* view = scene->FindComponent<ViewModel>();
    REQUIRE(holder);
    REQUIRE(view);
    holder->Select(1);
    for (int i = 0; i < 30; ++i) {
        step();
    }
    REQUIRE_FALSE(view->GetPartEntities().empty());
    const auto targetPrefab = assets.Load<Spot::Prefab>("prefabs/spawnables/props/target_dummy.prefab");
    REQUIRE(targetPrefab);
    auto* dummy = scene->Instantiate(targetPrefab);
    REQUIRE(dummy);
    REQUIRE(dummy->GetComponent<Spot::MeshRenderer>()->GetMesh());
    const auto* player = scene->FindComponent<PlayerController>();
    dummy->GetTransform().SetWorldPosition(player->GetEyePosition() + player->GetAimDirection() * 5 -
                                           Spot::Vector3{0, .19f, 0});
    auto target = device->CreateTexture(
        {.width = 960, .height = 540, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb});
    REQUIRE(renderer.IsValid());
    const std::array scenes{scene.get()};
    auto draw = [&](std::string_view name) {
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        renderer.Render(*context, scenes, target.get());
        device->EndFrame();
        auto pixels = device->DownloadTexture(*target);
        CHECK(Spot::Image(960, 540, pixels).SavePng(std::filesystem::path(SPOT_SANDBOX_CAPTURE_DIR) / name));
        return pixels;
    };
    std::filesystem::create_directories(SPOT_SANDBOX_CAPTURE_DIR);
    const auto idle = draw("pistol_idle.png");
    holder->Fire();
    step();
    CHECK(scene->FindComponent<PlayerController>()->GetRecoil() > 0);
    CHECK(draw("pistol_fire.png") != idle);
    input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Right});
    for (int i = 0; i < 30; ++i) {
        step();
    }
    CHECK(holder->IsAiming());
    CHECK(draw("pistol_sights.png") != idle);
    input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Right});

    // Every held item at the hip and down its sights, and a reload halfway.
    const std::array names{"physics_gun", "pistol", "rifle", "shotgun", "rocket_launcher", "tool_gun"};
    for (int slot = 0; slot < 6; ++slot) {
        holder->Select(slot);
        for (int i = 0; i < 40; ++i) {
            step();
        }
        const auto hip = draw(std::string(names[static_cast<std::size_t>(slot)]) + "_hip.png");
        if (holder->GetWeapon()) {
            input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Right});
            for (int i = 0; i < 30; ++i) {
                step();
            }
            CHECK(draw(std::string(names[static_cast<std::size_t>(slot)]) + "_sights.png") != hip);
            input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Right});
            for (int i = 0; i < 30; ++i) {
                step();
            }
        }
    }
    holder->Select(2);
    for (int i = 0; i < 40; ++i) {
        step();
    }
    holder->Fire();
    REQUIRE(holder->GetWeapon()->StartReload());
    while (holder->GetWeapon()->GetReloadProgress() < 0.45f) {
        step();
    }
    draw("rifle_reload.png");
}
