#include "gameplay_world.h"

#include "gameplay/camera/camera_shake.h"
#include "gameplay/effects/effect_library.h"
#include "gameplay/effects/flash.h"
#include "gameplay/effects/surface.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

namespace {

std::size_t CountParticles(Spot::Scene& scene) {
    std::size_t count = 0;
    scene.ForEachComponent<Spot::ParticleSystem>(
        [&](Spot::ParticleSystem& system) { count += system.GetParticles().size(); });
    return count;
}

int CountFlashes(Spot::Scene& scene) {
    int count = 0;
    scene.ForEachComponent<Gameplay::Flash>([&](const Gameplay::Flash&) { ++count; });
    return count;
}

Gameplay::ParticleLayer MakeLayer(std::uint32_t count) {
    Gameplay::ParticleLayer layer;
    layer.count = count;
    layer.settings.lifetime = {0.2f, 0.2f};
    return layer;
}

} // namespace

TEST_CASE("Effects are played by name from the scene's library", "[sandbox][gameplay]") {
    GameplayTests::World world;
    auto& library = world.scene.CreateEntity("Effects").AddComponent<Gameplay::EffectLibrary>();
    Gameplay::EffectDefinition burst{.name = "burst", .layers = {MakeLayer(10), MakeLayer(4)}};
    burst.light = {.color = {1.0f, 0.5f, 0.2f, 1.0f}, .range = 4.0f, .intensity = 10.0f, .lifetime = 0.1f};
    library.effects.push_back(burst);
    world.Step();

    Gameplay::EffectLibrary::Play(world.scene, "burst", {0.0f, 1.0f, 0.0f});
    CHECK(CountParticles(world.scene) == 14);
    CHECK(CountFlashes(world.scene) == 1);
    // More of them for a larger amount; nothing for an effect the library does not have.
    Gameplay::EffectLibrary::Play(world.scene, "burst", {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {.amount = 2.0f});
    CHECK(CountParticles(world.scene) == 14 + 28);
    Gameplay::EffectLibrary::Play(world.scene, "missing", {0.0f, 1.0f, 0.0f});
    CHECK(CountParticles(world.scene) == 42);
    // They are over by their lifetimes, and their emitters wait to be reused.
    world.Run(0.5f);
    CHECK(CountParticles(world.scene) == 0);
    CHECK(CountFlashes(world.scene) == 0);
    CHECK(Gameplay::EffectPool::Get(world.scene).GetEmitterCount() == 4);
}

TEST_CASE("Surfaces are kinds of the scene's library, the first by default", "[sandbox][gameplay]") {
    GameplayTests::World world;
    auto& library = world.scene.CreateEntity("Surfaces").AddComponent<Gameplay::SurfaceLibrary>();
    library.surfaces = {{.name = "Stone", .impactEffect = "impact.stone"}, {.name = "Glass", .impactEffect = "glass"}};
    auto& pane = world.scene.CreateEntity("Pane");
    pane.AddComponent<Gameplay::Surface>().kind = 1;
    world.Step();
    CHECK(Gameplay::SurfaceLibrary::Of(world.scene, &pane)->name == "Glass");
    CHECK(Gameplay::SurfaceLibrary::Of(world.scene, &world.ground)->name == "Stone");
    CHECK(Gameplay::SurfaceLibrary::Of(world.scene, nullptr)->name == "Stone");
    CHECK(library.Get("Glass") == library.Get(1));
}

TEST_CASE("Camera shakes reach the views within their radius", "[sandbox][gameplay]") {
    GameplayTests::World world;
    auto& nearView = world.scene.CreateEntity("Near").AddComponent<Gameplay::CameraShake>();
    auto& farView = world.scene.CreateEntity("Far").AddComponent<Gameplay::CameraShake>();
    farView.GetEntity().GetTransform().SetPosition({100.0f, 0.0f, 0.0f});
    world.Step();
    Gameplay::CameraShake::Add(world.scene, {0.0f, 0.0f, 0.0f}, 0.1f, 20.0f);
    world.Step();
    CHECK(nearView.GetRotation().w < 0.99999f);
    CHECK(farView.GetRotation().w == 1.0f);
    world.Run(1.5f);
    CHECK(nearView.GetRotation().w == 1.0f);
}
