#include "../registered_assets.h"
#include "developer_mode.h"
#include "game_controls.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/core/players.h"
#include "gameplay/game/game_mode.h"
#include "gameplay/network/player_info.h"
#include "main_menu.h"
#include "npc.h"
#include "player_appearance.h"
#include "prop_spawner.h"
#include "sandbox_hud.h"
#include "spawn_menu.h"
#include "temp_directory.h"
#include <fstream>

#include "gameplay/camera/first_person_view.h"
#include "gameplay/character/character.h"
#include "gameplay/character/character_visual.h"
#include "gameplay/environment/day_cycle.h"
#include "gameplay/health/health.h"
#include "gameplay/health/health_elements.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/inventory/view_model.h"
#include "gameplay/inventory/world_model.h"
#include "gameplay/menus/pause_menu.h"
#include "gameplay/vehicles/vehicle.h"
#include "gameplay/vehicles/vehicle_seat.h"
#include "gameplay/weapons/firearm.h"
#include <spot/scene/ui/ui_system.h>

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <string_view>
#include <vector>

using Gameplay::DayCycle;

TEST_CASE("The player's inspection body and seated occupants render through the vehicle windows",
          "[gpu][sandbox][player_body][offscreen]") {
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
    auto scene = Spot::InstantiateScene(assets, assets.FindByPath("scenes/empty.scene").value());
    REQUIRE(scene);
    for (int frame = 0; frame < 45; ++frame) {
        scene->Update(1.0f / 60);
    }
    auto* character = Gameplay::Players::FindComponent<Gameplay::Character>(*scene);
    auto* visual = Gameplay::Players::FindComponent<Gameplay::CharacterVisual>(*scene);
    REQUIRE(character);
    REQUIRE(visual);
    auto* body = scene->GetEntity(visual->modelRoot);
    REQUIRE(body);
    auto target = device->CreateTexture(
        {.width = 1024, .height = 768, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb, .sampleCount = 1});
    const std::filesystem::path output(SPOT_SANDBOX_CAPTURE_DIR);
    std::filesystem::create_directories(output);
    const auto render = [&](const std::string& name) {
        scene->PrepareToRender();
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        renderer.Render(*context, *scene, target.get());
        device->EndFrame();
        auto pixels = device->DownloadTexture(*target);
        REQUIRE(pixels.size() == 1024 * 768 * 4);
        if (!name.empty()) {
            CHECK(Spot::Image(1024, 768, pixels).SavePng(output / (name + ".png")));
        }
        return pixels;
    };
    const auto difference = [](const auto& a, const auto& b) {
        std::size_t changed = 0;
        for (std::size_t i = 0; i < a.size(); i += 4) {
            changed += std::abs(static_cast<int>(a[i]) - b[i]) + std::abs(static_cast<int>(a[i + 1]) - b[i + 1]) +
                                   std::abs(static_cast<int>(a[i + 2]) - b[i + 2]) >
                               12
                           ? 1
                           : 0;
        }
        return changed;
    };
    const auto firstPerson = render("player_firstperson");
    REQUIRE(character->SetThirdPerson(true));
    scene->Update(0);
    const auto thirdPerson = render("player_thirdperson");
    CHECK(difference(firstPerson, thirdPerson) > 5000);
    auto* view = character->GetEntity().GetComponent<Gameplay::FirstPersonView>();
    REQUIRE(view);
    view->SetLook(0, 0.45f);
    scene->Update(0);
    const auto lookingUp = render("player_thirdperson_up");
    view->SetLook(0, -0.45f);
    scene->Update(0);
    const auto lookingDown = render("player_thirdperson_down");
    CHECK(difference(lookingUp, lookingDown) > 5000);
    view->SetLook(0, -0.15f);
    scene->Update(0);
    auto& observer = scene->CreateEntity("Driver inspection");
    auto& observerCamera = observer.AddComponent<Spot::Camera>();
    observerCamera.SetEnabled(false);
    observerCamera.SetFieldOfView(Spot::ToRadians(55.0f));
    for (const auto* name : {"car", "coupe", "helicopter"}) {
        CAPTURE(name);
        auto* entity = scene->Instantiate(
            assets.Load<Spot::Prefab>(std::string("prefabs/spawnables/vehicles/") + name + ".prefab"));
        REQUIRE(entity);
        entity->GetTransform().SetWorldPosition({0, 0.05f, 0});
        entity->GetComponent<Spot::Rigidbody>()->SetKinematic(true);
        auto* vehicle = entity->GetComponent<Gameplay::Vehicle>();
        REQUIRE(vehicle);
        scene->Update(0);
        REQUIRE(vehicle->GetSeats().size() == 2);
        for (auto* seat : vehicle->GetSeats()) {
            CAPTURE(seat->driver);
            REQUIRE(seat->Enter(*character));
            scene->Update(0);
            const auto* possessed = Gameplay::Players::GetPawn(*scene);
            REQUIRE(possessed);
            auto* activeCamera = scene->GetEntity(possessed->GetViewCamera())->GetComponent<Spot::Camera>();
            activeCamera->SetEnabled(false);
            observerCamera.SetEnabled(true);
            const auto focus = seat->GetSeatPosition() + Spot::Vector3{0, 0.45f, 0};
            observer.GetTransform().SetWorldPosition(focus + Spot::Vector3{seat->driver ? -1.9f : 1.9f, 0.35f, -1.3f});
            observer.GetTransform().SetWorldRotation(
                Spot::Quaternion::LookRotation(focus - observer.GetTransform().GetWorldPosition()));
            const auto occupied = render(std::string(seat->driver ? "player_seated_" : "player_passenger_") + name);
            body->SetActive(false);
            const auto empty = render("");
            CHECK(difference(occupied, empty) > 100);
            body->SetActive(true);
            observerCamera.SetEnabled(false);
            activeCamera->SetEnabled(true);
            seat->Exit();
        }
        entity->Destroy();
        scene->Update(0);
    }
}

TEST_CASE("Developer smite renders a lightning bolt from the sky to its victim",
          "[gpu][sandbox][developer][powers][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Tests::RegisteredAssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
    struct Restore {
        Spot::AssetManager* previous = Spot::Assets::GetManager();
        ~Restore() { Spot::Detail::SetAssetManager(previous); }
    } restore;
    Spot::Detail::SetAssetManager(&assets);
    Spot::Detail::LoadGameCode();
    Spot::Scene scene;
    auto* player = scene.Instantiate(assets.Load<Spot::Prefab>("prefabs/actors/player.prefab"));
    REQUIRE(player);
    scene.CreateEntity("Controller").AddComponent<Gameplay::PlayerController>().pawn = player->GetId();
    scene.Update(0);
    auto* mode = player->GetComponent<DeveloperMode>();
    REQUIRE(mode);
    mode->Unlock();
    REQUIRE(DeveloperMode::UseLocalPower(scene, DeveloperMode::Power::Smite, "Player"));
    CHECK(player->GetComponent<Gameplay::Health>()->IsDead());
    scene.Update(1.0f / 60);
    scene.ForEachComponent<Spot::Camera>([](auto& camera) { camera.SetEnabled(false); });
    auto& camera = scene.CreateEntity("Lightning inspection");
    camera.GetTransform().SetPosition({4, 11, -36});
    camera.GetTransform().SetRotation(
        Spot::Quaternion::LookRotation(Spot::Vector3{0, 11, 0} - camera.GetTransform().GetPosition()));
    auto& view = camera.AddComponent<Spot::Camera>();
    view.SetFieldOfView(Spot::ToRadians(45.0f));
    view.SetBackground({0.015f, 0.015f, 0.025f, 1});
    auto target = device->CreateTexture(
        {.width = 768, .height = 1024, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb, .sampleCount = 1});
    scene.PrepareToRender();
    auto* context = device->BeginOffscreenFrame(*target);
    REQUIRE(context);
    renderer.Render(*context, scene, target.get());
    device->EndFrame();
    const auto pixels = device->DownloadTexture(*target);
    REQUIRE(pixels.size() == 768 * 1024 * 4);
    const std::filesystem::path output(SPOT_SANDBOX_CAPTURE_DIR);
    std::filesystem::create_directories(output);
    CHECK(Spot::Image(768, 1024, pixels).SavePng(output / "developer_smite.png"));
    std::size_t blue = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        blue += pixels[i + 2] > 140 && int(pixels[i + 2]) > int(pixels[i]) + 30 ? 1 : 0;
    }
    CHECK(blue > 100);
}

TEST_CASE("Random player appearances render alongside the authored developer skin",
          "[gpu][sandbox][appearance][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Tests::RegisteredAssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
    struct Restore {
        Spot::AssetManager* previous = Spot::Assets::GetManager();
        ~Restore() { Spot::Detail::SetAssetManager(previous); }
    } restore;
    Spot::Detail::SetAssetManager(&assets);
    Spot::Detail::LoadGameCode();
    Spot::Scene scene;
    const auto bodyPrefab = assets.Load<Spot::Prefab>("prefabs/actors/player_body.prefab");
    const auto developerSkin = assets.Load<Spot::Model>("generated/models/developer.gltf");
    REQUIRE(bodyPrefab);
    REQUIRE(developerSkin);
    for (int i = 0; i < 7; ++i) {
        auto& player = scene.CreateEntity("Appearance");
        player.GetTransform().SetPosition({static_cast<float>(i - 3) * 0.85f, 0, 0});
        auto* body = scene.Instantiate(bodyPrefab);
        REQUIRE(body);
        body->SetParent(&player);
        player.AddComponent<Gameplay::CharacterVisual>().modelRoot = body->GetId();
        player.AddComponent<PlayerAppearance>();
        if (i == 6) {
            auto& developer = player.AddComponent<DeveloperMode>();
            developer.skin = developerSkin;
            developer.Unlock();
        }
    }
    scene.ForEachComponent<Spot::Camera>([](auto& camera) { camera.SetEnabled(false); });
    scene.Update(0);
    scene.ForEachComponent<PlayerAppearance>([&](PlayerAppearance& appearance) {
        CHECK(appearance.GetSeed() != 0);
        const auto* visual = appearance.GetEntity().GetComponent<Gameplay::CharacterVisual>();
        REQUIRE(visual);
        auto* body = scene.GetEntity(visual->modelRoot);
        REQUIRE(body);
        auto* hair = body->FindChild("Hair");
        REQUIRE(hair);
        auto* renderer = hair->GetComponent<Spot::SkinnedMeshRenderer>();
        REQUIRE(renderer);
        CHECK(renderer->GetMesh());
        CHECK(renderer->GetMaterial());
        CHECK(renderer->GetAnimator() == body->GetId());
    });
    auto& camera = scene.CreateEntity("Appearance inspection");
    camera.GetTransform().SetPosition({0, 0.15f, -6.0f});
    camera.AddComponent<Spot::Camera>().SetFieldOfView(Spot::ToRadians(30.0f));
    camera.GetTransform().SetRotation(Spot::Quaternion::LookRotation({0, 0, 1}));
    auto& light = scene.CreateEntity("Key light");
    light.GetTransform().SetRotation(Spot::Quaternion::LookRotation({-0.5f, -1, 0.5f}));
    light.AddComponent<Spot::DirectionalLight>();
    scene.CreateEntity("Fill light").AddComponent<Spot::AmbientLight>();
    auto target = device->CreateTexture(
        {.width = 1536, .height = 640, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb, .sampleCount = 1});
    scene.PrepareToRender();
    auto* context = device->BeginOffscreenFrame(*target);
    REQUIRE(context);
    renderer.Render(*context, scene, target.get());
    device->EndFrame();
    const auto pixels = device->DownloadTexture(*target);
    REQUIRE(pixels.size() == 1536 * 640 * 4);
    const std::filesystem::path output(SPOT_SANDBOX_CAPTURE_DIR);
    std::filesystem::create_directories(output);
    CHECK(Spot::Image(1536, 640, pixels).SavePng(output / "player_appearances.png"));
}

TEST_CASE("The developer skin renders a black DEV shirt, pixel sunglasses and a gold crown",
          "[gpu][sandbox][developer][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Tests::RegisteredAssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
    struct Restore {
        Spot::AssetManager* previous = Spot::Assets::GetManager();
        ~Restore() { Spot::Detail::SetAssetManager(previous); }
    } restore;
    Spot::Detail::SetAssetManager(&assets);
    Spot::Detail::LoadGameCode();
    Spot::Scene scene;
    auto* player = scene.Instantiate(assets.Load<Spot::Prefab>("prefabs/actors/player.prefab"));
    REQUIRE(player);
    scene.ForEachComponent<Spot::Camera>([](auto& camera) { camera.SetEnabled(false); });
    auto* mode = player->GetComponent<DeveloperMode>();
    REQUIRE(mode);
    mode->Unlock();
    player->GetComponent<Gameplay::Inventory>()->SetEnabled(false);
    scene.Update(0);
    auto* visual = player->GetComponent<Gameplay::CharacterVisual>();
    REQUIRE(visual);
    auto* body = scene.GetEntity(visual->modelRoot);
    REQUIRE(body);
    for (const auto* name : {"DEV Lettering", "Thug Life Glasses", "Developer Crown"}) {
        CAPTURE(name);
        auto* child = body->FindChild(name);
        REQUIRE(child);
        auto* mesh = child->GetComponent<Spot::SkinnedMeshRenderer>();
        REQUIRE(mesh);
        CHECK(mesh->GetMesh());
        CHECK(mesh->GetAnimator() == body->GetId());
    }
    auto& camera = scene.CreateEntity("Developer inspection");
    camera.GetTransform().SetPosition({0.15f, 0.2f, -3.5f});
    camera.GetTransform().SetRotation(
        Spot::Quaternion::LookRotation(Spot::Vector3{0, 0.15f, 0} - camera.GetTransform().GetPosition()));
    auto& view = camera.AddComponent<Spot::Camera>();
    view.SetFieldOfView(Spot::ToRadians(38.0f));
    view.SetBackground({0.12f, 0.15f, 0.19f, 1});
    auto& light = scene.CreateEntity("Key light");
    light.GetTransform().SetRotation(Spot::Quaternion::LookRotation({-0.5f, -1, 0.5f}));
    light.AddComponent<Spot::DirectionalLight>();
    scene.CreateEntity("Fill light").AddComponent<Spot::AmbientLight>();
    auto target = device->CreateTexture(
        {.width = 1024, .height = 1024, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb, .sampleCount = 1});
    scene.PrepareToRender();
    auto* context = device->BeginOffscreenFrame(*target);
    REQUIRE(context);
    renderer.Render(*context, scene, target.get());
    device->EndFrame();
    auto pixels = device->DownloadTexture(*target);
    REQUIRE(pixels.size() == 1024 * 1024 * 4);
    const std::filesystem::path output(SPOT_SANDBOX_CAPTURE_DIR);
    std::filesystem::create_directories(output);
    CHECK(Spot::Image(1024, 1024, pixels).SavePng(output / "developer_skin.png"));
    std::size_t gold = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        gold += pixels[i] > 140 && pixels[i + 1] > 70 && pixels[i + 2] < pixels[i + 1] / 2 ? 1 : 0;
    }
    CHECK(gold > 100);
}

TEST_CASE("Armed and unarmed humanoid prefabs render with articulated bodies and a mounted pistol",
          "[gpu][sandbox][humanoid][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Tests::RegisteredAssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
    struct Restore {
        Spot::AssetManager* assets = Spot::Assets::GetManager();
        ~Restore() { Spot::Detail::SetAssetManager(assets); }
    } restore;
    Spot::Detail::SetAssetManager(&assets);
    Spot::Detail::LoadGameCode();
    Spot::Scene scene;
    auto* unarmed = scene.Instantiate(assets.Load<Spot::Prefab>("prefabs/actors/npc.prefab"));
    auto* armed = scene.Instantiate(assets.Load<Spot::Prefab>("prefabs/actors/npc_armed.prefab"));
    REQUIRE(unarmed);
    REQUIRE(armed);
    unarmed->GetTransform().SetPosition({-0.6f, 0, 0});
    armed->GetTransform().SetPosition({0.6f, 0, 0});
    unarmed->GetComponent<Npc>()->patrolDistance = 0;
    armed->GetComponent<Npc>()->patrolDistance = 0;
    auto& floor = scene.CreateEntity("Floor");
    floor.GetTransform().SetPosition({0, -0.98f, 0});
    floor.AddComponent<Spot::BoxCollider>(Spot::Vector3{10, 0.1f, 10});
    floor.AddComponent<Spot::MeshRenderer>(Spot::Mesh::Create(*device, Spot::PrimitiveMeshes::CreatePlane(10)),
                                           Spot::Material{.baseColor = {0.22f, 0.24f, 0.28f, 1}});
    auto& camera = scene.CreateEntity("Camera");
    camera.GetTransform().SetPosition({2.2f, 1.2f, -3.8f});
    camera.GetTransform().SetRotation(Spot::Quaternion::LookRotation(-camera.GetTransform().GetPosition()));
    camera.AddComponent<Spot::Camera>().SetBackground({0.08f, 0.09f, 0.12f, 1});
    auto& light = scene.CreateEntity("Light");
    light.GetTransform().SetRotation(Spot::Quaternion::LookRotation({-0.5f, -1, 0.5f}));
    light.AddComponent<Spot::DirectionalLight>();
    scene.CreateEntity("Ambient").AddComponent<Spot::AmbientLight>();
    for (int frame = 0; frame < 6; ++frame) {
        scene.Update(1.0f / 60);
    }
    const auto* visual = armed->GetComponent<Gameplay::WorldModel>();
    REQUIRE(visual);
    REQUIRE(visual->GetModelEntity());
    CHECK_FALSE(unarmed->GetComponent<Gameplay::WorldModel>()->GetModelEntity());
    const auto* animator = armed->GetComponent<Spot::Animator>();
    REQUIRE(animator);
    const auto elbow = animator->GetSkeleton()->FindBone("RightForearm");
    REQUIRE(elbow);
    CHECK(std::abs(animator->GetLocalPose()[*elbow].rotation.x) > 0.1f);
    const auto forward = visual->GetModelEntity()->GetTransform().GetWorldRotation() * Spot::Vector3{0, 0, -1};
    CHECK(Spot::Dot(forward, Spot::Vector3{0, 0, -1}) > 0.95f);
    auto target = device->CreateTexture(
        {.width = 1024, .height = 768, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb, .sampleCount = 1});
    scene.PrepareToRender();
    auto* context = device->BeginOffscreenFrame(*target);
    REQUIRE(context);
    renderer.Render(*context, scene, target.get());
    device->EndFrame();
    const auto pixels = device->DownloadTexture(*target);
    REQUIRE(pixels.size() == 1024 * 768 * 4);
    std::size_t shirtPixels = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        shirtPixels += pixels[i + 2] > pixels[i] + 20 && pixels[i + 1] > pixels[i] + 10 ? 1 : 0;
    }
    CHECK(shirtPixels > 1000);
    const std::filesystem::path output(SPOT_SANDBOX_CAPTURE_DIR);
    std::filesystem::create_directories(output);
    CHECK(Spot::Image(1024, 768, pixels).SavePng(output / "humanoid_npcs.png"));
    // From the side, closer, to see how the hands hold the pistol.
    camera.GetTransform().SetPosition({2.4f, 0.9f, 0.4f});
    camera.GetTransform().SetRotation(
        Spot::Quaternion::LookRotation(Spot::Vector3{0.6f, 0.3f, 0} - camera.GetTransform().GetPosition()));
    scene.PrepareToRender();
    context = device->BeginOffscreenFrame(*target);
    REQUIRE(context);
    renderer.Render(*context, scene, target.get());
    device->EndFrame();
    CHECK(Spot::Image(1024, 768, device->DownloadTexture(*target)).SavePng(output / "humanoid_npcs_side.png"));
    auto* npc = armed->GetComponent<Npc>();
    npc->SetEnabled(false);
    auto* animated = armed->GetComponent<Spot::Animator>();
    animated->SetRootMotion(Spot::RootMotionMode::None);
    animated->SetController(assets.Load<Spot::AnimatorController>("animation/player.controller"));
    npc->SetState("Aiming", true);
    for (int frame = 0; frame < 45; ++frame) {
        scene.Update(1.0f / 60);
    }
    scene.PrepareToRender();
    const auto aimedForward = visual->GetModelEntity()->GetTransform().GetWorldRotation() * Spot::Vector3{0, 0, -1};
    CHECK(Spot::Dot(aimedForward, Spot::Vector3{0, 0, -1}) > 0.99f);
    const auto capturePose = [&](const char* name) {
        scene.PrepareToRender();
        auto* frame = device->BeginOffscreenFrame(*target);
        REQUIRE(frame);
        renderer.Render(*frame, scene, target.get());
        device->EndFrame();
        CHECK(Spot::Image(1024, 768, device->DownloadTexture(*target)).SavePng(output / name));
    };
    capturePose("humanoid_aiming.png");
    for (const auto& [name, direction] : std::array{std::pair{"humanoid_walk_backward.png", Spot::Vector2{0, -1}},
                                                    std::pair{"humanoid_strafe.png", Spot::Vector2{1, 0}},
                                                    std::pair{"humanoid_run.png", Spot::Vector2{0, 2}}}) {
        animated->SetFloat("sideways", direction.x);
        animated->SetFloat("forward", direction.y);
        scene.Update(0.18f);
        capturePose(name);
    }
    animated->SetFloat("forward", 0);
    animated->SetFloat("sideways", 0);
    animated->SetBool("crouching", true);
    scene.Update(0.2f);
    capturePose("humanoid_crouch.png");
    animated->SetFloat("forward", 1);
    scene.Update(0.25f);
    capturePose("humanoid_crouch_walk.png");
    animated->SetBool("airborne", true);
    animated->SetFloat("vertical", 0.8f);
    for (int frame = 0; frame < 12; ++frame) {
        scene.Update(1.0f / 60);
    }
    CHECK(animated->GetControllerRuntime().GetState() == 2);
    capturePose("humanoid_jump_rise.png");
    animated->SetFloat("vertical", -0.8f);
    for (int frame = 0; frame < 12; ++frame) {
        scene.Update(1.0f / 60);
    }
    capturePose("humanoid_jump_fall.png");
    animated->SetBool("airborne", false);
    npc->SetState("Aiming", false);
    npc->SetState(visual->sprintState, true);
    animated->SetBool("crouching", false);
    animated->SetFloat("forward", 2);
    for (int frame = 0; frame < 45; ++frame) {
        scene.Update(1.0f / 60);
    }
    capturePose("humanoid_sprint_lowered.png");
    const auto sprintForward = visual->GetModelEntity()->GetTransform().GetWorldRotation() * Spot::Vector3{0, 0, -1};
    CHECK(sprintForward.y < -0.4f);
    npc->SetState(visual->sprintState, false);
    npc->SetState("Aiming", true);

    animated->SetBool("crouching", false);
    animated->SetFloat("forward", 0);
    const auto checkHands = [&] {
        for (const auto& hand : {visual->rightHand, visual->leftHand}) {
            const auto* limb = hand.Get(scene);
            REQUIRE(limb);
            const auto* grip = scene.GetEntity(limb->GetTarget());
            REQUIRE(grip);
            const auto bone = animated->GetSkeleton()->FindBone(limb->GetBone());
            REQUIRE(bone);
            const auto offset = limb->GetOffset().translation;
            const auto goal = grip->GetTransform().GetWorldMatrix() * Spot::Vector4{offset.x, offset.y, offset.z, 1};
            const auto actual =
                armed->GetTransform().GetWorldMatrix() * animated->GetModelPose()[*bone] * Spot::Vector4{0, 0, 0, 1};
            CHECK(Spot::Length(Spot::Vector3{goal.x - actual.x, goal.y - actual.y, goal.z - actual.z}) < 0.025f);
        }
    };
    auto* inventory = armed->GetComponent<Gameplay::Inventory>();
    REQUIRE(inventory);
    for (const auto* weapon : {"pistol", "rifle", "shotgun", "rocket_launcher", "physics_gun", "tool_gun"}) {
        CAPTURE(weapon);
        auto* held = inventory->AddFromPrefab(
            assets.Load<Spot::Prefab>(std::string("prefabs/spawnables/weapons/") + weapon + ".prefab"));
        REQUIRE(held);
        inventory->Select(*held);
        for (const bool aiming : {false, true}) {
            CAPTURE(aiming);
            npc->SetState("Aiming", aiming);
            for (int frame = 0; frame < 50; ++frame) {
                scene.Update(1.0f / 60);
            }
            scene.PrepareToRender();
            checkHands();
            const auto instance = visual->GetModelEntity()->GetTransform().GetWorldMatrix();
            if (std::string_view(weapon) == "rifle" || std::string_view(weapon) == "shotgun") {
                const auto chest = *animated->GetSkeleton()->FindBone("Chest");
                const auto receiver =
                    Spot::Inverse(armed->GetTransform().GetWorldMatrix() * animated->GetModelPose()[chest]) * instance *
                    Spot::Vector4{0, 0, 0, 1};
                CHECK(receiver.z < -0.15f);
                const float butt = std::string_view(weapon) == "rifle" ? 0.345f : 0.394f;
                for (const auto* name : {"Neck", "Head"}) {
                    const auto bone = *animated->GetSkeleton()->FindBone(name);
                    const auto stock =
                        Spot::Inverse(armed->GetTransform().GetWorldMatrix() * animated->GetModelPose()[bone]) *
                        instance * Spot::Vector4{0, -0.01f, butt, 1};
                    CHECK((std::abs(stock.x) > 0.12f || std::abs(stock.z) > 0.13f || stock.y < -0.08f));
                }
            }
            const auto prefix = std::string("humanoid_") + (aiming ? "aiming_" : "ready_") + weapon;
            camera.GetTransform().SetPosition({2.4f, 0.9f, 0.4f});
            camera.GetTransform().SetRotation(
                Spot::Quaternion::LookRotation(Spot::Vector3{0.6f, 0.3f, 0} - camera.GetTransform().GetPosition()));
            capturePose((prefix + "_side.png").c_str());
            camera.GetTransform().SetPosition({0.6f, 0.65f, -2.4f});
            camera.GetTransform().SetRotation(
                Spot::Quaternion::LookRotation(Spot::Vector3{0.6f, 0.25f, 0} - camera.GetTransform().GetPosition()));
            capturePose((prefix + "_front.png").c_str());
            animated->SetBool("crouching", true);
            for (int frame = 0; frame < 25; ++frame) {
                scene.Update(1.0f / 60);
            }
            scene.PrepareToRender();
            checkHands();
            animated->SetBool("crouching", false);
        }
        npc->SetState("Aiming", false);
        npc->SetState(visual->sprintState, true);
        animated->SetFloat("forward", 2);
        for (int frame = 0; frame < 50; ++frame) {
            scene.Update(1.0f / 60);
        }
        scene.PrepareToRender();
        checkHands();
        camera.GetTransform().SetPosition({2.4f, 0.9f, 0.4f});
        camera.GetTransform().SetRotation(
            Spot::Quaternion::LookRotation(Spot::Vector3{0.6f, 0.3f, 0} - camera.GetTransform().GetPosition()));
        capturePose((std::string("humanoid_sprint_") + weapon + "_side.png").c_str());
        npc->SetState(visual->sprintState, false);
        animated->SetFloat("forward", 0);
    }
}

TEST_CASE("Sandbox map selection and empty debug floor render", "[gpu][sandbox][maps][offscreen]") {
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
    const auto game =
        Spot::GameSettings::LoadFromFile(std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() / "game.json");
    REQUIRE(game);
    game->sections.input.Apply(actions);
    actions.SetDefaults();
    Spot::Detail::LoadGameCode();
    auto target = device->CreateTexture(
        {.width = 1280, .height = 720, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb});
    REQUIRE(renderer.IsValid());
    const std::filesystem::path output(SPOT_SANDBOX_CAPTURE_DIR);
    std::filesystem::create_directories(output);
    for (const auto* name : {"main_menu", "empty"}) {
        CAPTURE(name);
        const auto path = std::string("scenes/") + name + ".scene";
        const auto id = assets.FindByPath(path);
        REQUIRE(id);
        auto scene = Spot::InstantiateScene(assets, *id);
        REQUIRE(scene);
        for (int frame = 0; frame < 3; ++frame) {
            scene->Update(std::string_view(name) == "main_menu" ? 0.1f : 0.0f);
            auto* context = device->BeginOffscreenFrame(*target);
            REQUIRE(context);
            renderer.Render(*context, *scene, target.get());
            device->EndFrame();
        }
        const auto pixels = device->DownloadTexture(*target);
        REQUIRE(pixels.size() == 1280 * 720 * 4);
        std::size_t visible = 0;
        for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 4) {
            visible += pixels[pixel] > 60 || pixels[pixel + 1] > 60 || pixels[pixel + 2] > 60;
        }
        CHECK(visible > 10000);
        CHECK(Spot::Image(1280, 720, pixels).SavePng(output / (std::string(name) + ".png")));
        if (auto* menu = scene->FindComponent<MainMenu>()) {
            const auto* view = scene->GetEntity(menu->previewImage)->GetComponent<Spot::UiImage>();
            REQUIRE(view->texture);
            const auto homeTexture = view->texture;
            menu->ShowPage(MainMenu::Page::Play);
            std::vector<std::uint8_t> previousPreview;
            for (std::size_t map = 0; map < menu->maps.size(); ++map) {
                menu->SelectMap(map);
                scene->Update(0.2f);
                auto* context = device->BeginOffscreenFrame(*target);
                REQUIRE(context);
                renderer.Render(*context, *scene, target.get());
                device->EndFrame();
                const auto* preview = scene->GetEntity(menu->previewImage)->GetComponent<Spot::UiImage>();
                REQUIRE(preview->texture);
                const auto previewPixels = device->DownloadTexture(*preview->texture);
                REQUIRE(previewPixels.size() == 1600 * 900 * 4);
                if (map == 0)
                    CHECK(preview->texture == homeTexture);
                else
                    CHECK(previewPixels != previousPreview);
                previousPreview = previewPixels;
                const auto pagePixels = device->DownloadTexture(*target);
                CHECK(Spot::Image(1280, 720, pagePixels)
                          .SavePng(output / ("main_menu_map_" + std::to_string(map) + ".png")));
            }
            for (const auto& [page, file] : {std::pair{MainMenu::Page::Host, "main_menu_host.png"},
                                             std::pair{MainMenu::Page::Join, "main_menu_join.png"},
                                             std::pair{MainMenu::Page::Connecting, "main_menu_connecting.png"}}) {
                menu->ShowPage(page);
                scene->Update(0.2f);
                auto* context = device->BeginOffscreenFrame(*target);
                REQUIRE(context);
                renderer.Render(*context, *scene, target.get());
                device->EndFrame();
                const auto pagePixels = device->DownloadTexture(*target);
                CHECK(pagePixels != pixels);
                CHECK(Spot::Image(1280, 720, pagePixels).SavePng(output / file));
            }
            menu->ShowPage(MainMenu::Page::Settings);
            scene->Update(0.2f);
            auto* context = device->BeginOffscreenFrame(*target);
            REQUIRE(context);
            renderer.Render(*context, *scene, target.get());
            device->EndFrame();
            const auto settingsPixels = device->DownloadTexture(*target);
            CHECK(Spot::Image(1280, 720, settingsPixels).SavePng(output / "main_menu_settings.png"));
            menu->ShowPage(MainMenu::Page::Quit);
            scene->Update(0.2f);
            context = device->BeginOffscreenFrame(*target);
            REQUIRE(context);
            renderer.Render(*context, *scene, target.get());
            device->EndFrame();
            const auto quitPixels = device->DownloadTexture(*target);
            CHECK(Spot::Image(1280, 720, quitPixels).SavePng(output / "main_menu_quit.png"));
            CHECK(Spot::Input::IsCursorLocked() == false);
        }
        if (auto* pause = scene->FindComponent<Gameplay::PauseMenu>()) {
            pause->Open();
            scene->Update(0.2f);
            auto* context = device->BeginOffscreenFrame(*target);
            REQUIRE(context);
            renderer.Render(*context, *scene, target.get());
            device->EndFrame();
            const auto paused = device->DownloadTexture(*target);
            REQUIRE(paused.size() == pixels.size());
            CHECK(paused != pixels);
            CHECK(Spot::Image(1280, 720, paused).SavePng(output / "empty_pause.png"));
            pause->Close();
        }
    }
}

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
    auto scene = Spot::InstantiateScene(assets, assets.FindByPath("scenes/playground.scene").value());
    REQUIRE(scene);
    REQUIRE(scene->FindEntity("Workshop"));
    // The HUD, made as the game starts, would cover the views.
    scene->Update(0.0f);
    scene->ForEachComponent<Spot::Camera>([](Spot::Camera& camera) { camera.SetEnabled(false); });
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
        // The sky: clouds by day and at sunset, stars and the moon at night, and flying through the clouds and over
        // their tops.
        View{"sky_clouds.png", {0, 3, 30}, {30, 45, -60}, 0.45f},
        View{"sky_sunset.png", {0, 3, 30}, {-80, 20, 0}, 0.735f},
        View{"sky_night.png", {0, 3, 30}, {-40, 35, -30}, 0.02f},
        View{"sky_in_clouds.png", {0, 1700, 30}, {600, 1750, -600}, 0.4f},
        View{"sky_above_clouds.png", {0, 2700, 30}, {600, 2400, -600}, 0.4f},
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
        // The volumetric clouds even out their noise over frames, as they do while the game runs.
        const int frames = std::string_view(view.name).starts_with("sky_") ? 12 : 2;
        for (int frame = 0; frame < frames; ++frame) {
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

TEST_CASE("Generated weapons render their material finishes and machined silhouettes up close",
          "[gpu][sandbox][weapons][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Tests::RegisteredAssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
    struct RestoreAssets {
        Spot::AssetManager* previous = Spot::Assets::GetManager();
        ~RestoreAssets() { Spot::Detail::SetAssetManager(previous); }
    } restore;
    Spot::Detail::SetAssetManager(&assets);
    auto target = device->CreateTexture(
        {.width = 1280, .height = 720, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb, .sampleCount = 1});
    REQUIRE(renderer.IsValid());
    const std::filesystem::path output(SPOT_SANDBOX_CAPTURE_DIR);
    std::filesystem::create_directories(output);
    for (const char* name : {"pistol", "rifle", "shotgun", "rocket_launcher", "tool_gun", "physics_gun", "ray_gun"}) {
        CAPTURE(name);
        const auto model = assets.Load<Spot::Model>(std::string("generated/models/") + name + ".gltf");
        REQUIRE(model);
        Spot::Scene scene;
        scene.Instantiate(*model, name);
        auto& camera = scene.CreateEntity("Inspection camera");
        auto& view = camera.AddComponent<Spot::Camera>();
        view.SetFieldOfView(Spot::ToRadians(38.0f));
        view.SetNearPlane(0.01f);
        view.SetBackground({0.12f, 0.15f, 0.19f, 1});
        auto& light = scene.CreateEntity("Key light");
        light.GetTransform().SetRotation(Spot::Quaternion::LookRotation({-0.6f, -1, -0.4f}));
        light.AddComponent<Spot::DirectionalLight>();
        scene.CreateEntity("Fill light").AddComponent<Spot::AmbientLight>();
        auto& rim = scene.CreateEntity("Rim light");
        rim.GetTransform().SetRotation(Spot::Quaternion::LookRotation({0.8f, -0.3f, 0.5f}));
        rim.AddComponent<Spot::DirectionalLight>(Spot::Color{0.7f, 0.8f, 1.0f}, 0.7f);
        const bool compact = std::string_view(name) == "pistol" || std::string_view(name) == "tool_gun" ||
                             std::string_view(name) == "physics_gun" || std::string_view(name) == "ray_gun";
        const Spot::Vector3 focus = compact ? Spot::Vector3{0, -0.025f, -0.07f} : Spot::Vector3{0, -0.02f, -0.15f};
        const float distance = compact ? 0.48f : 1.55f;
        for (const bool side : {true, false}) {
            camera.GetTransform().SetPosition(
                focus + (side ? Spot::Vector3{distance, 0.04f, 0}
                              : Spot::Vector3{distance * 0.78f, distance * 0.32f, distance * 0.55f}));
            camera.GetTransform().SetRotation(
                Spot::Quaternion::LookRotation(focus - camera.GetTransform().GetPosition()));
            scene.PrepareToRender();
            auto* context = device->BeginOffscreenFrame(*target);
            REQUIRE(context);
            renderer.Render(*context, scene, target.get());
            device->EndFrame();
            const auto pixels = device->DownloadTexture(*target);
            REQUIRE(pixels.size() == 1280 * 720 * 4);
            CHECK(Spot::Image(1280, 720, pixels)
                      .SavePng(output / (std::string(name) + (side ? "_detail_side.png" : "_detail_perspective.png"))));
            std::size_t silhouette = 0;
            for (std::size_t i = 0; i < pixels.size(); i += 4) {
                const int difference = std::abs(int(pixels[i]) - int(pixels[0])) +
                                       std::abs(int(pixels[i + 1]) - int(pixels[1])) +
                                       std::abs(int(pixels[i + 2]) - int(pixels[2]));
                silhouette += difference > 20 ? 1 : 0;
            }
            CHECK(silhouette > 1000);
            CHECK(silhouette < 1280 * 720 / 2);
        }
    }
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
    auto scene = Spot::InstantiateScene(assets, assets.FindByPath("scenes/playground.scene").value());
    REQUIRE(scene);
    auto step = [&] {
        actions.Update(input);
        scene->Update(Spot::Scene::DefaultFixedTimeStep);
        input.BeginFrame();
    };
    for (int i = 0; i < 90; ++i) {
        step();
    }
    auto* holder = Gameplay::Players::FindComponent<Gameplay::Inventory>(*scene);
    auto* view = Gameplay::Players::FindComponent<Gameplay::ViewModel>(*scene);
    REQUIRE(holder);
    REQUIRE(view);
    const auto firearm = [&] { return dynamic_cast<Gameplay::Firearm*>(holder->GetEquipped()); };
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
    const auto* player = Gameplay::Players::FindComponent<Gameplay::FirstPersonView>(*scene);
    dummy->GetTransform().SetWorldPosition(player->GetEyePosition() + player->GetAimDirection() * 5 -
                                           Spot::Vector3{0, .19f, 0});
    auto target = device->CreateTexture(
        {.width = 960, .height = 540, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb});
    REQUIRE(renderer.IsValid());
    auto draw = [&](std::string_view name) {
        const auto& arms = view->GetArmEntities();
        REQUIRE(arms.size() == 4);
        const auto* skin = view->armsMaterialSource.Get(*scene);
        REQUIRE(skin);
        REQUIRE(skin->GetMaterial());
        for (std::size_t side = 0; side < 2; ++side) {
            const auto* forearm = scene->GetEntity(arms[side * 2]);
            const auto* hand = scene->GetEntity(arms[side * 2 + 1]);
            REQUIRE(forearm);
            REQUIRE(hand);
            CHECK(forearm->GetComponent<Spot::MeshRenderer>()->GetMaterial() == skin->GetMaterial());
            CHECK(hand->GetComponent<Spot::MeshRenderer>()->GetMaterial() == skin->GetMaterial());
            const auto end = forearm->GetTransform().GetWorldMatrix() * Spot::Vector4{0, 0, -1, 1};
            CHECK(Spot::Distance(Spot::Vector3{end.x, end.y, end.z}, hand->GetTransform().GetWorldPosition()) <
                  0.0001f);
        }
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        renderer.Render(*context, *scene, target.get());
        device->EndFrame();
        auto pixels = device->DownloadTexture(*target);
        CHECK(Spot::Image(960, 540, pixels).SavePng(std::filesystem::path(SPOT_SANDBOX_CAPTURE_DIR) / name));
        return pixels;
    };
    std::filesystem::create_directories(SPOT_SANDBOX_CAPTURE_DIR);
    REQUIRE(view->GetArmEntities().size() == 4);
    const auto idle = draw("pistol_idle.png");
    for (auto id : view->GetArmEntities()) {
        scene->GetEntity(id)->SetActive(false);
    }
    const auto withoutArms = draw("pistol_without_arms.png");
    std::size_t armPixels = 0;
    for (std::size_t i = 0; i < idle.size(); i += 4) {
        armPixels += std::abs(int(idle[i]) - int(withoutArms[i])) +
                                 std::abs(int(idle[i + 1]) - int(withoutArms[i + 1])) +
                                 std::abs(int(idle[i + 2]) - int(withoutArms[i + 2])) >
                             20
                         ? 1
                         : 0;
    }
    CHECK(armPixels > 1000);
    for (auto id : view->GetArmEntities()) {
        scene->GetEntity(id)->SetActive(true);
    }
    firearm()->Fire();
    step();
    CHECK(Gameplay::Players::FindComponent<Gameplay::FirstPersonView>(*scene)->GetRecoil() > 0);
    CHECK(draw("pistol_fire.png") != idle);
    input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Right});
    for (int i = 0; i < 30; ++i) {
        step();
    }
    CHECK(firearm()->IsAiming());
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
        if (firearm()) {
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
    firearm()->Fire();
    REQUIRE(firearm()->StartReload());
    while (firearm()->GetReloadProgress() < 0.45f) {
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
    auto scene = Spot::InstantiateScene(assets, assets.FindByPath("scenes/playground.scene").value());
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
    auto* holder = Gameplay::Players::FindComponent<Gameplay::Inventory>(*scene);
    auto* health = Gameplay::Players::FindComponent<Gameplay::Health>(*scene);
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

    auto* dropped = holder->Drop();
    REQUIRE(dropped);
    run(3);
    holder->OpenWheel();
    run(3);
    CHECK(draw("hud_wheel_dropped.png") != inHand);
    holder->CloseWheel(false);
    REQUIRE(holder->PickUp(*dropped));
    const auto carried = holder->GetItems();
    for (auto* item : carried) {
        if (item != dropped) {
            item->GetEntity().SetActive(false);
        }
    }
    run(3);
    holder->OpenWheel();
    run(3);
    CHECK(draw("hud_wheel_single.png") != inHand);
    holder->CloseWheel(false);
    for (auto* item : carried) {
        item->GetEntity().SetActive(true);
    }
    holder->Select(*dropped);
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
    health->TakeDamage({.amount = 78.0f});
    run(6);
    draw("hud_hurt_night.png");

    health->TakeDamage({.amount = 100.0f});
    run(30);
    REQUIRE(health->IsDead());
    draw("hud_dead.png");
    run(static_cast<int>(Gameplay::GameMode::Get(*scene)->respawnTime * 60.0f) + 10);
    REQUIRE_FALSE(health->IsDead());

    // The menus, once the spawn menu's thumbnails, two a frame, are all drawn.
    auto* ui = scene->FindComponent<SpawnMenu>();
    REQUIRE(ui);
    day->timeOfDay = 0.45f;
    ui->Open();
    for (int frame = 0; frame < 30; ++frame) {
        step();
        draw("menu_spawn.png");
    }
    ui->SelectCategory(Spawnable::Category::Weapon);
    step();
    draw("menu_spawn_weapons.png");
    ui->SelectCategory(Spawnable::Category::Vehicle);
    for (int frame = 0; frame < 3; ++frame) {
        step();
        draw("menu_spawn_vehicles.png");
    }
    ui->SelectCategory(Spawnable::Category::Prop);
    ui->Search("barrel");
    step();
    draw("menu_spawn_search.png");
    ui->Search("");
    ui->SetCompactView(true);
    step();
    draw("menu_spawn_compact.png");
    ui->SetCompactView(false);
    scene->FindComponent<Gameplay::PauseMenu>()->Open();
    step();
    draw("menu_pause.png");
    Gameplay::Menu::CloseAll(*scene);

    // Placing what the menu gave, and the controls on Help.
    auto* spawner = Gameplay::Players::FindComponent<PropSpawner>(*scene);
    REQUIRE(spawner);
    REQUIRE(spawner->BeginPlacement(0));
    run(2);
    draw("hud_placing.png");
    spawner->CancelPlacement();
    tap(Spot::Key::H);
    draw("hud_help.png");
    tap(Spot::Key::H);
}

TEST_CASE("Authored sandbox menus render their labels and controls offscreen", "[gpu][ui][sandbox][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Tests::RegisteredAssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
    Spot::InputState input;
    Spot::InputActionMap actions;
    struct AssetScope {
        Spot::AssetManager* previous = Spot::Assets::GetManager();
        Spot::InputState* input = Spot::Input::GetState();
        Spot::InputActionMap* actions = Spot::Input::GetActionMap();
        float timeScale = Spot::Time::GetTimeScale();
        ~AssetScope() {
            Spot::Detail::SetAssetManager(previous);
            Spot::Input::SetState(input);
            Spot::Input::SetActionMap(actions);
            Spot::Time::SetTimeScale(timeScale);
        }
    } scope;
    Spot::Detail::SetAssetManager(&assets);
    Spot::Input::SetState(&input);
    Spot::Input::SetActionMap(&actions);
    Spot::Detail::LoadGameCode();
    const auto settings = Spot::GameSettings::LoadFromFile(std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() /
                                                           Spot::GameSettings::FileName);
    REQUIRE(settings);
    settings->sections.input.Apply(actions);
    const auto prefab = assets.Load<Spot::Prefab>("prefabs/interface.prefab");
    REQUIRE(prefab);
    Spot::Scene scene;
    REQUIRE(scene.Instantiate(prefab, {.assets = &assets}));
    auto* pause = scene.FindComponent<Gameplay::PauseMenu>();
    REQUIRE(pause);
    pause->Open();
    auto target = device->CreateTexture(
        {.width = 1280, .height = 720, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb,
                                           .sampleCount = 1,
                                           .shadowMapSize = 0,
                                           .localShadowMapSize = 0});
    auto* context = device->BeginOffscreenFrame(*target);
    REQUIRE(context);
    renderer.Render(*context, scene, target.get());
    device->EndFrame();
    const auto pixels = device->DownloadTexture(*target);
    REQUIRE(pixels.size() == 1280 * 720 * 4);
    std::size_t bright = 0;
    for (std::size_t index = 0; index < pixels.size(); index += 4)
        if (pixels[index] > 180 && pixels[index + 1] > 180 && pixels[index + 2] > 180)
            ++bright;
    CHECK(bright > 1500);
    CHECK(scene.GetUpdateCount() == 0);
    const auto capture = Spot::Process::GetEnvironmentValue("SPOT_UI_CAPTURE_DIR");
    if (capture && !capture->empty()) {
        std::filesystem::create_directories(*capture);
        CHECK(Spot::Image(1280, 720, pixels).SavePng(std::filesystem::path(*capture) / "pause_menu.png"));
    }
}

TEST_CASE("Nearby player names and the immortal hexagon render above the head", "[gpu][sandbox][labels][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Tests::RegisteredAssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
    struct Restore {
        Spot::AssetManager* previous = Spot::Assets::GetManager();
        ~Restore() { Spot::Detail::SetAssetManager(previous); }
    } restore;
    Spot::Detail::SetAssetManager(&assets);
    Spot::Detail::LoadGameCode();
    Spot::Scene scene;
    auto& observer = scene.CreateEntity("Observer");
    auto& viewer = observer.AddComponent<Gameplay::Pawn>();
    observer.GetTransform().SetPosition({0.15f, 0.3f, -4.6f});
    observer.GetTransform().SetRotation(
        Spot::Quaternion::LookRotation(Spot::Vector3{0, 0.2f, 0} - observer.GetTransform().GetPosition()));
    auto& inspection = observer.AddComponent<Spot::Camera>();
    inspection.SetFieldOfView(Spot::ToRadians(38.0f));
    inspection.SetBackground({0.12f, 0.15f, 0.19f, 1});
    viewer.viewCamera = observer.GetId();
    scene.CreateEntity("Controller").AddComponent<Gameplay::PlayerController>().pawn = observer.GetId();
    auto& viewerInfo = scene.CreateEntity("Viewer info").AddComponent<Gameplay::PlayerInfo>();
    viewerInfo.pawn = observer.GetId();
    auto* character = scene.Instantiate(assets.Load<Spot::Prefab>("prefabs/actors/player.prefab"));
    REQUIRE(character);
    auto& target = *character;
    target.GetComponent<Gameplay::Inventory>()->SetEnabled(false);
    auto& health = *target.GetComponent<Gameplay::Health>();
    auto& mode = *target.GetComponent<DeveloperMode>();
    auto& keyLight = scene.CreateEntity("Key light");
    keyLight.GetTransform().SetRotation(Spot::Quaternion::LookRotation({-0.5f, -1, 0.5f}));
    keyLight.AddComponent<Spot::DirectionalLight>();
    scene.CreateEntity("Fill light").AddComponent<Spot::AmbientLight>();
    auto& info = scene.CreateEntity("Developer info").AddComponent<Gameplay::PlayerInfo>();
    info.name = "lucasof";
    info.pawn = target.GetId();
    auto& hud = scene.CreateEntity("Hud").AddComponent<Gameplay::Hud>();
    hud.font = assets.Load<Spot::Font>("fonts/Inter-Regular.ttf");
    REQUIRE(hud.font);
    auto& controls = scene.CreateEntity("Controls").AddComponent<GameControls>();
    controls.spawnNpcs = false;
    controls.cheats = false;
    scene.Update(0);
    mode.Unlock();
    CHECK_FALSE(mode.IsAuthenticated());
    TempDirectory keyDirectory;
    DeveloperMode::Seed seed{};
    const auto keyPath = keyDirectory.GetPath() / DeveloperMode::KeyFileName;
    std::ofstream(keyPath) << std::string(64, '0') << '\n';
    mode.SetPublicKey(DeveloperMode::DerivePublicKey(seed));
    mode.SetKeyPath(keyPath);
    auto* controller = Gameplay::Players::Get(scene);
    REQUIRE(controller);
    controller->Possess(*target.GetComponent<Gameplay::Pawn>());
    scene.Update(0);
    REQUIRE(mode.IsAuthenticated());
    controller->Possess(viewer);
    scene.Update(0);
    scene.ForEachComponent<Spot::Camera>([&](auto& camera) { camera.SetEnabled(&camera == &inspection); });
    health.SetGod(true);
    health.TakeDamage({.amount = 100, .instigator = observer.GetId()});
    REQUIRE(mode.GetImmortalTime() > 0);
    REQUIRE(GetPlayerLabels(scene, {1024, 768}).size() == 1);
    CHECK(GetPlayerLabels(scene, {1024, 768})[0].developerAuthenticated);
    auto image = device->CreateTexture(
        {.width = 1024, .height = 768, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(image);
    Spot::SceneRenderer renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb, .sampleCount = 1});
    scene.PrepareToRender();
    auto* context = device->BeginOffscreenFrame(*image);
    REQUIRE(context);
    renderer.Render(*context, scene, image.get());
    device->EndFrame();
    const auto pixels = device->DownloadTexture(*image);
    REQUIRE(pixels.size() == 1024 * 768 * 4);
    std::size_t purple = 0;
    std::size_t white = 0;
    std::size_t blue = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        purple += pixels[i] > 80 && pixels[i + 2] > pixels[i] + 30 && pixels[i + 1] < 100 ? 1 : 0;
        blue += pixels[i + 2] > pixels[i] + 60 && pixels[i + 2] > pixels[i + 1] + 20 ? 1 : 0;
        white += pixels[i] > 180 && pixels[i + 1] > 180 && pixels[i + 2] > 180 ? 1 : 0;
    }
    CHECK(purple > 1000);
    CHECK(white > 100);
    CHECK(blue > 100);
    const std::filesystem::path output(SPOT_SANDBOX_CAPTURE_DIR);
    std::filesystem::create_directories(output);
    CHECK(Spot::Image(1024, 768, pixels).SavePng(output / "player_immortal_label.png"));
    CHECK(Spot::Image(1024, 768, pixels).SavePng(output / "lucasof_developer.png"));
}

TEST_CASE("The authored HUD renders compact ammo, infinity and outlined kill feed icons",
          "[gpu][sandbox][hud][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    REQUIRE(device);
    Spot::Tests::RegisteredAssetManager assets(SANDBOX_IMPORTED_DIR, device.get());
    struct Restore {
        Spot::AssetManager* assets = Spot::Assets::GetManager();
        ~Restore() { Spot::Detail::SetAssetManager(assets); }
    } restore;
    Spot::Detail::SetAssetManager(&assets);
    Spot::Detail::LoadGameCode();
    Spot::Scene scene;
    auto& player = scene.CreateEntity("Player");
    player.AddComponent<Gameplay::Pawn>();
    player.AddComponent<Gameplay::KillFeedSubject>();
    player.AddComponent<Gameplay::Health>();
    auto& inventory = player.AddComponent<Gameplay::Inventory>();
    auto& rifle = player.CreateChild("Rifle").AddComponent<Gameplay::Firearm>();
    rifle.displayName = "Rifle";
    rifle.icon = assets.Load<Spot::Texture>("generated/textures/icon_rifle.png");
    auto& ray = player.CreateChild("Ray Gun").AddComponent<Gameplay::Firearm>();
    ray.displayName = "Ray Gun";
    ray.infiniteAmmo = true;
    ray.icon = assets.Load<Spot::Texture>("generated/textures/icon_ray_gun.png");
    REQUIRE(rifle.icon);
    REQUIRE(ray.icon);
    scene.CreateEntity("Controller").AddComponent<Gameplay::PlayerController>().pawn = player.GetId();
    auto& hud = scene.CreateEntity("HUD").AddComponent<Gameplay::Hud>();
    hud.interface = assets.Load<Spot::Prefab>("prefabs/interface.prefab");
    REQUIRE(hud.interface);
    scene.Update(Spot::Scene::DefaultFixedTimeStep);
    scene.Update(Spot::Scene::DefaultFixedTimeStep);
    auto& enemy = scene.CreateEntity("Enemy");
    enemy.AddComponent<Gameplay::Pawn>();
    enemy.AddComponent<Gameplay::KillFeedSubject>();
    auto& ally = scene.CreateEntity("Ally");
    ally.AddComponent<Gameplay::Pawn>();
    ally.AddComponent<Gameplay::KillFeedSubject>();
    auto& damage = Gameplay::DamageSystem::Get(scene);
    damage.Report(
        {enemy.GetId(), {.type = "Bullet", .instigator = player.GetId(), .source = rifle.GetEntity().GetId()}}, true);
    damage.Report({ally.GetId(), {.type = "Fall"}}, true);
    damage.Report({enemy.GetId(), {.type = "Smite", .instigator = ally.GetId()}}, true);
    damage.Report({enemy.GetId(), {.type = "Explosion", .instigator = ally.GetId()}}, true);
    damage.Report({ally.GetId(), {.type = "Melee", .instigator = enemy.GetId()}}, true);
    scene.Update(Spot::Scene::DefaultFixedTimeStep);
    constexpr Spot::Vector2 Size{1280, 720};
    auto target = device->CreateTexture(
        {.width = 1280, .height = 720, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
    REQUIRE(target);
    Spot::Renderer2D renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8Srgb});
    REQUIRE(renderer.IsValid());
    std::filesystem::create_directories(SPOT_SANDBOX_CAPTURE_DIR);
    const auto draw = [&](std::string_view name) {
        auto* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        {
            auto pass = context->BeginRenderPass({.target = target.get()});
            renderer.Begin(pass, Spot::UiSystem::ScreenCamera(Size), Size);
            // Bright and dark scenery stand-ins expose contrast problems without allocating the 3D playground.
            renderer.DrawQuad({640, 540}, {1280, 360}, {0.7f, 0.8f, 0.9f, 1});
            renderer.DrawQuad({640, 180}, {1280, 360}, {0.07f, 0.1f, 0.13f, 1});
            scene.GetOrAddSystem<Spot::UiSystem>().DrawUi(renderer, Size);
            renderer.End();
        }
        device->EndFrame();
        const auto pixels = device->DownloadTexture(*target);
        REQUIRE(Spot::Image(1280, 720, pixels).SavePng(std::filesystem::path(SPOT_SANDBOX_CAPTURE_DIR) / name));
    };
    draw("hud_layout_rifle.png");
    inventory.Select(ray);
    scene.Update(Spot::Scene::DefaultFixedTimeStep);
    draw("hud_layout_ray_gun.png");
}
