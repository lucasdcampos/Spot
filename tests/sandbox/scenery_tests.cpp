#include "../registered_assets.h"
#include "gameplay/vehicles/vehicle_health.h"
#include "gameplay/vehicles/wheeled_vehicle.h"

#include "gameplay/environment/day_cycle.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <vector>

using Gameplay::DayCycle;

namespace {

struct View {
    const char* name;
    Spot::Vector3 position;
    Spot::Vector3 target;
};

// The cooked playground by day, drawn offscreen from a camera of its own, without the HUD.
struct OffscreenPlayground {
    std::unique_ptr<Spot::GraphicsDevice> device = Spot::GraphicsDevice::CreateOffscreen({.debug = true});
    Spot::Tests::RegisteredAssetManager assets{SANDBOX_IMPORTED_DIR, device.get()};
    Spot::AssetManager* previousAssets = Spot::Assets::GetManager();
    std::unique_ptr<Spot::Scene> scene;
    Spot::Entity* camera = nullptr;
    std::unique_ptr<Spot::Texture> target;
    std::unique_ptr<Spot::SceneRenderer> renderer;

    OffscreenPlayground() {
        REQUIRE(device);
        Spot::Detail::SetAssetManager(&assets);
        Spot::Detail::LoadGameCode();
        const auto settings = Spot::GameSettings::LoadFromFile(
            std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() / Spot::GameSettings::FileName);
        REQUIRE(settings);
        scene = Spot::InstantiateScene(assets, assets.FindByPath("scenes/playground.scene").value());
        REQUIRE(scene);
        // The HUD, made as the game starts, would cover the views.
        scene->Update(0.0f);
        scene->ForEachComponent<Spot::Camera>([](Spot::Camera& camera) { camera.SetEnabled(false); });
        if (auto* game = scene->FindEntity("Game")) {
            game->ForEachChild([](Spot::Entity& child) { child.SetActive(false); });
        }
        camera = &scene->CreateEntity("Acceptance Camera");
        camera->AddComponent<Spot::Camera>().SetFarPlane(600.0f);
        auto* day = scene->FindComponent<DayCycle>();
        REQUIRE(day);
        day->timeOfDay = 0.42f;
        target = device->CreateTexture(
            {.width = 960, .height = 540, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
        REQUIRE(target);
        renderer = std::make_unique<Spot::SceneRenderer>(
            *device, Spot::SceneRendererConfig{.targetFormat = Spot::TextureFormat::Rgba8Srgb});
        REQUIRE(renderer->IsValid());
    }
    ~OffscreenPlayground() {
        scene.reset();
        Spot::Detail::SetAssetManager(previousAssets);
    }

    // Draws each view, saves it under the name in the captures' folder and checks it with the predicate.
    void Capture(std::span<const View> views, const std::function<void(const std::vector<std::uint8_t>&)>& check) {
        const std::filesystem::path output(SPOT_SANDBOX_CAPTURE_DIR);
        std::filesystem::create_directories(output);
        for (const View& view : views) {
            camera->GetTransform().SetPosition(view.position);
            camera->GetTransform().SetRotation(Spot::Quaternion::LookRotation(view.target - view.position));
            scene->Update(0.0f);
            for (int frame = 0; frame < 2; ++frame) {
                auto* context = device->BeginOffscreenFrame(*target);
                REQUIRE(context);
                renderer->Render(*context, *scene, target.get());
                device->EndFrame();
            }
            auto pixels = device->DownloadTexture(*target);
            REQUIRE(pixels.size() == 960 * 540 * 4);
            CAPTURE(view.name);
            check(pixels);
            CHECK(Spot::Image(960, 540, std::move(pixels)).SavePng(output / view.name));
        }
    }
};

} // namespace

TEST_CASE("The cooked playground renders its grass and trees up close", "[gpu][sandbox][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    OffscreenPlayground playground;
    // The lawn at a player's feet, an oak on it, the pines on the hills and the hills from the valley.
    const std::array views{
        View{"scenery_grass.png", {0, 1.7f, 42}, {-1, 0, 38}},
        View{"scenery_oak.png", {3, 1.7f, 52}, {-6, 3.5f, 44}},
        View{"scenery_oak_leaves.png", {-2.5f, 5.5f, 46}, {-6, 5.2f, 44}},
        View{"scenery_forest.png", {-52, 6, 52}, {-75, 12, 75}},
        View{"scenery_hills.png", {-40, 4, 30}, {-80, 10, 70}},
    };
    playground.Capture(views, [](const std::vector<std::uint8_t>& pixels) {
        // Most of each view is lit green: grass, leaves and needles.
        std::size_t green = 0;
        for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 4) {
            green +=
                pixels[pixel + 1] > pixels[pixel] && pixels[pixel + 1] > pixels[pixel + 2] && pixels[pixel + 1] > 25;
        }
        CHECK(green > 960 * 540 / 4);
    });
}

TEST_CASE("The cooked playground's car shows its paint, glass, lights and wheels", "[gpu][sandbox][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    OffscreenPlayground playground;
    const auto* car = playground.scene->FindComponent<Gameplay::WheeledVehicle>();
    REQUIRE(car);
    const Spot::Vector3 at = car->GetEntity().GetTransform().GetWorldPosition();
    const Spot::Quaternion rotation = car->GetEntity().GetTransform().GetWorldRotation();
    const auto around = [&](Spot::Vector3 offset) { return at + rotation * offset; };
    const Spot::Vector3 middle = around({0.0f, 0.7f, 0.0f});
    // Its front three quarters, its side at the height of its wheels, and its rear from above, as its camera sees it.
    const std::array views{
        View{"car_front.png", around({-3.6f, 1.5f, -4.6f}), middle},
        View{"car_side.png", around({5.5f, 0.6f, 0.0f}), middle},
        View{"car_rear.png", around({1.2f, 2.4f, 6.0f}), middle},
    };
    playground.Capture(views, [](const std::vector<std::uint8_t>& pixels) {
        // Much of each view is its red paint.
        std::size_t red = 0;
        for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 4) {
            red += pixels[pixel] > 2 * pixels[pixel + 1] && pixels[pixel] > 2 * pixels[pixel + 2] && pixels[pixel] > 40;
        }
        CHECK(red > 960 * 540 / 40);
    });
}

TEST_CASE("The exploded car renders as a dark burned wreck", "[gpu][sandbox][offscreen][vehicles][damage]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No usable graphics device");
    }
    OffscreenPlayground playground;
    auto* car = playground.scene->FindComponent<Gameplay::WheeledVehicle>();
    REQUIRE(car);
    auto* health = car->GetEntity().GetComponent<Gameplay::VehicleHealth>();
    REQUIRE(health);
    const auto at = car->GetTransform().GetWorldPosition();
    const auto rotation = car->GetTransform().GetWorldRotation();
    const auto position = at + rotation * Spot::Vector3{-3.6f, 1.5f, -4.6f};
    const auto middle = at + Spot::Vector3{0.0f, 0.7f, 0.0f};
    const auto redPixels = [](const std::vector<std::uint8_t>& pixels) {
        std::size_t red = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            red += pixels[i] > 2 * pixels[i + 1] && pixels[i] > 2 * pixels[i + 2] && pixels[i] > 40;
        }
        return red;
    };
    std::size_t paint = 0;
    const std::array before{View{"car_before_explosion.png", position, middle}};
    playground.Capture(before, [&](const auto& pixels) { paint = redPixels(pixels); });
    REQUIRE(paint > 960 * 540 / 40);
    const auto blastPosition = at + rotation * Spot::Vector3{-13.0f, 5.0f, -15.0f};
    const auto blastTarget = at + Spot::Vector3{0.0f, 2.0f, 0.0f};
    std::vector<std::uint8_t> background;
    const std::array beforeBlast{View{"car_before_blast.png", blastPosition, blastTarget}};
    playground.Capture(beforeBlast, [&](const auto& pixels) { background = pixels; });
    health->TakeDamage({.amount = health->maxHealth});
    REQUIRE(health->IsDestroyed());
    for (int frame = 0; frame < 10; ++frame) {
        playground.scene->Update(Spot::Scene::DefaultFixedTimeStep);
    }
    const std::array explosion{View{"car_explosion.png", blastPosition, blastTarget}};
    playground.Capture(explosion, [&](const auto& pixels) {
        std::size_t changed = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            changed += std::abs(int(pixels[i]) - int(background[i])) +
                           std::abs(int(pixels[i + 1]) - int(background[i + 1])) +
                           std::abs(int(pixels[i + 2]) - int(background[i + 2])) >
                       60;
        }
        CHECK(changed > 960 * 540 / 60);
    });
    for (int frame = 0; frame < 230; ++frame) {
        playground.scene->Update(Spot::Scene::DefaultFixedTimeStep);
    }
    const auto wreckMiddle = car->GetTransform().GetWorldPosition() +
                             car->GetTransform().GetWorldRotation() * Spot::Vector3{0.0f, 0.7f, 0.0f};
    const std::array after{
        View{"car_burned.png", wreckMiddle + rotation * Spot::Vector3{-3.6f, 1.5f, -4.6f}, wreckMiddle}};
    playground.Capture(after, [&](const auto& pixels) { CHECK(redPixels(pixels) < paint / 5); });
}
