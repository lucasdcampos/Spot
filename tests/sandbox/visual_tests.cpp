#include "../registered_assets.h"
#include "day_cycle.h"
#include "player_controller.h"
#include "player_health.h"
#include "prop_spawner.h"
#include "runtime_interface.h"
#include "view_model.h"
#include "weapon.h"
#include "weapon_holder.h"
#include <spot/scene/ui/ui_system.h>

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
    Spot::Tests::RegisteredAssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
    auto* previousAssets = Spot::Assets::GetManager();
    Spot::Detail::SetAssetManager(&assets);
    struct RestoreAssets {
        Spot::AssetManager* previous;
        ~RestoreAssets() { Spot::Detail::SetAssetManager(previous); }
    } restore{previousAssets};
    Spot::Detail::LoadGameCode();
    const auto settings = Spot::GameSettings::LoadFromFile(std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() /
                                                           Spot::GameSettings::FileName);
    REQUIRE(settings);
    auto scene = Spot::InstantiateScene(assets, settings->startupScene);
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
        // The sky: clouds by day and at sunset, stars and the moon at night.
        View{"sky_clouds.png", {0, 3, 30}, {30, 45, -60}, 0.45f},
        View{"sky_sunset.png", {0, 3, 30}, {-80, 20, 0}, 0.735f},
        View{"sky_night.png", {0, 3, 30}, {-40, 35, -30}, 0.02f},
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
        for (int frame = 0; frame < 2; ++frame) {
            auto* context = device->BeginOffscreenFrame(*target);
            REQUIRE(context);
            renderer.Render(*context, *scene, target.get());
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
    auto* context = device->BeginOffscreenFrame(*target);
    REQUIRE(context);
    renderer.Render(*context, *scene, target.get());
    device->EndFrame();
    const auto withoutGlass = device->DownloadTexture(*target);
    for (auto* window : windows) {
        window->SetEnabled(true);
    }
    scene->Update(0.0f);
    context = device->BeginOffscreenFrame(*target);
    REQUIRE(context);
    renderer.Render(*context, *scene, target.get());
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
    Spot::Tests::RegisteredAssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
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
    Spot::Detail::LoadGameCode();
    const auto settings = Spot::GameSettings::LoadFromFile(std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() /
                                                           Spot::GameSettings::FileName);
    REQUIRE(settings);
    settings->sections.input.Apply(actions);
    auto scene = Spot::InstantiateScene(assets, settings->startupScene);
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
    auto draw = [&](std::string_view name) {
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        renderer.Render(*context, *scene, target.get());
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

TEST_CASE("The cooked sandbox draws its HUD over the game: in hand, the wheel, hurt at night, running and dead",
          "[gpu][sandbox][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Tests::RegisteredAssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
    Spot::InputState input;
    Spot::InputActionMap actions;
    struct Restore {
        Spot::AssetManager* assets = Spot::Assets::GetManager();
        Spot::InputState* input = Spot::Input::GetState();
        Spot::InputActionMap* actions = Spot::Input::GetActionMap();
        float timeScale = Spot::Time::GetTimeScale();
        ~Restore() {
            Spot::Detail::SetAssetManager(assets);
            Spot::Input::SetState(input);
            Spot::Input::SetActionMap(actions);
            Spot::Time::SetTimeScale(timeScale);
        }
    } restore;
    Spot::Detail::SetAssetManager(&assets);
    Spot::Input::SetState(&input);
    Spot::Input::SetActionMap(&actions);
    Spot::Detail::LoadGameCode();
    const auto settings = Spot::GameSettings::LoadFromFile(std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() /
                                                           Spot::GameSettings::FileName);
    REQUIRE(settings);
    settings->sections.input.Apply(actions);
    auto scene = Spot::InstantiateScene(assets, settings->startupScene);
    REQUIRE(scene);
    auto step = [&] {
        actions.Update(input);
        scene->Update(Spot::Scene::DefaultFixedTimeStep * Spot::Time::GetTimeScale());
        input.BeginFrame();
    };
    auto run = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            step();
        }
    };
    run(90);
    auto* holder = scene->FindComponent<WeaponHolder>();
    auto* health = scene->FindComponent<PlayerHealth>();
    auto* day = scene->FindComponent<DayCycle>();
    REQUIRE(holder);
    REQUIRE(health);
    REQUIRE(day);
    constexpr std::uint32_t Width = 1280;
    constexpr std::uint32_t Height = 720;
    const Spot::Vector2 size{static_cast<float>(Width), static_cast<float>(Height)};
    auto target = device->CreateTexture(
        {.width = Width, .height = Height, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb});
    Spot::Renderer2D interface(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb});
    REQUIRE(renderer.IsValid());
    REQUIRE(interface.IsValid());
    std::filesystem::create_directories(SPOT_SANDBOX_CAPTURE_DIR);
    auto draw = [&](std::string_view name) {
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        renderer.Render(*context, *scene, target.get());
        {
            auto pass = context->BeginRenderPass({.target = target.get()});
            interface.Begin(pass, Spot::UiSystem::ScreenCamera(size), size);
            scene->GetOrAddSystem<Spot::UiSystem>().DrawUi(interface, size);
            interface.End();
        }
        device->EndFrame();
        auto pixels = device->DownloadTexture(*target);
        CHECK(Spot::Image(Width, Height, pixels).SavePng(std::filesystem::path(SPOT_SANDBOX_CAPTURE_DIR) / name));
        return pixels;
    };
    auto tap = [&](Spot::Key key) {
        input.OnEvent(Spot::KeyPressedEvent{key, false});
        step();
        input.OnEvent(Spot::KeyReleasedEvent{key});
        step();
    };

    tap(Spot::Key::Digit3);
    run(30);
    const auto inHand = draw("hud_rifle.png");

    input.OnEvent(Spot::KeyPressedEvent{Spot::Key::Tab, false});
    step();
    input.OnEvent(Spot::MouseMovedEvent{{700, 360}, {60, 60}});
    run(3);
    REQUIRE(holder->IsWheelOpen());
    CHECK(draw("hud_wheel.png") != inHand);
    input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::Tab});
    run(3);

    input.OnEvent(Spot::KeyPressedEvent{Spot::Key::W, false});
    input.OnEvent(Spot::KeyPressedEvent{Spot::Key::LeftShift, false});
    run(40);
    CHECK(draw("hud_running.png") != inHand);
    input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::LeftShift});
    input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::W});
    run(30);

    tap(Spot::Key::Digit6);
    day->timeOfDay = 0.92f;
    health->Damage(78.0f);
    run(6);
    draw("hud_hurt_night.png");

    health->Damage(100.0f);
    run(30);
    REQUIRE(health->IsDead());
    draw("hud_dead.png");
    run(static_cast<int>(health->respawnTime * 60.0f) + 10);
    REQUIRE_FALSE(health->IsDead());

    // The menus, once the spawn menu's thumbnails, two a frame, are all drawn.
    auto* ui = scene->FindComponent<RuntimeInterface>();
    REQUIRE(ui);
    day->timeOfDay = 0.45f;
    ui->OpenSpawnMenu();
    for (int frame = 0; frame < 30; ++frame) {
        step();
        draw("menu_spawn.png");
    }
    ui->SelectCategory(Spawnable::Category::Weapon);
    step();
    draw("menu_spawn_weapons.png");
    ui->SelectCategory(Spawnable::Category::Prop);
    ui->Search("barrel");
    step();
    draw("menu_spawn_search.png");
    ui->Search("");
    ui->SetCompactView(true);
    step();
    draw("menu_spawn_compact.png");
    ui->SetCompactView(false);
    ui->OpenPauseMenu();
    step();
    draw("menu_pause.png");
    ui->CloseMenus();

    // Placing what the menu gave, and the controls on Help.
    auto* spawner = scene->FindComponent<PropSpawner>();
    REQUIRE(spawner);
    REQUIRE(spawner->BeginPlacement(0));
    run(2);
    draw("hud_placing.png");
    spawner->CancelPlacement();
    tap(Spot::Key::H);
    draw("hud_help.png");
    tap(Spot::Key::H);
}
