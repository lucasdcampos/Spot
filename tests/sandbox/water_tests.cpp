#include "playground_fixture.h"

#include "gameplay/camera/first_person_view.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/weapons/explosive.h"
#include "gameplay/weapons/firearm.h"
#include "gameplay/weapons/projectile.h"

#include "gameplay/core/trace_volume.h"
#include "gameplay/effects/effect_library.h"
#include "gameplay/environment/water.h"

#include <catch2/catch_approx.hpp>

#include <cmath>
#include <string_view>

using Gameplay::Water;

namespace {

void RunFor(SandboxTests::Playground& game, float seconds) {
    game.Run(static_cast<int>(std::ceil(seconds / Spot::Scene::DefaultFixedTimeStep)));
}

Spot::Entity& Player(SandboxTests::Playground& game) {
    Spot::Entity* player = game.scene.FindEntity("Player");
    REQUIRE(player);
    return *player;
}

std::size_t ParticleCount(Spot::Scene& scene, const Gameplay::ParticleLayer& effect) {
    std::size_t count = 0;
    scene.ForEachComponent<Spot::ParticleSystem>([&](Spot::ParticleSystem& system) {
        if (system.GetSettings().startColor == effect.settings.startColor &&
            system.GetSettings().facing == effect.settings.facing)
            count += system.GetParticles().size();
    });
    return count;
}

// A layer of an effect of the playground's EffectLibrary.
const Gameplay::ParticleLayer& Layer(Spot::Scene& scene, std::string_view effect, std::size_t index = 0) {
    const auto* found = Gameplay::EffectLibrary::Find(scene, effect);
    REQUIRE(found);
    REQUIRE(index < found->layers.size());
    return found->layers[index];
}

Water& Pond(SandboxTests::Playground& game) {
    Spot::Entity* entity = game.scene.FindEntity("Pond Water");
    REQUIRE(entity);
    Water* water = entity->GetComponent<Water>();
    REQUIRE(water);
    return *water;
}

// Where a body can be put in the pond's water: its middle, a little below the surface.
Spot::Vector3 PondMiddle(Water& water) {
    return water.GetEntity().GetTransform().GetWorldPosition() - Spot::Vector3{0, 0.3f, 0};
}

// Puts the player above the pond, so its aim meets the water two meters below its eyes at the middle.
Spot::Vector3 AimAtPond(SandboxTests::Playground& game, Water& water) {
    Spot::Entity& player = Player(game);
    const auto& controller = *player.GetComponent<Gameplay::FirstPersonView>();
    const Spot::Vector3 aim = controller.GetAimDirection();
    REQUIRE(aim.y < -0.05f);
    const Spot::Vector3 target = water.GetEntity().GetTransform().GetWorldPosition();
    const Spot::Vector3 eye = target - aim * (2.0f / -aim.y);
    const Spot::Vector3 offset = controller.GetEyePosition() - player.GetTransform().GetPosition();
    player.GetTransform().SetPosition(eye - offset);
    return target;
}

Spot::Entity& Crate(Spot::Scene& scene, Spot::Vector3 position, float mass) {
    auto& crate = scene.CreateEntity("Water Test Crate");
    crate.GetTransform().SetPosition(position);
    crate.AddComponent<Spot::BoxCollider>(Spot::Vector3{0.5f, 0.5f, 0.5f});
    crate.AddComponent<Spot::Rigidbody>().SetMass(mass);
    return crate;
}

} // namespace

TEST_CASE("The pond is water that rays from above meet at its surface", "[sandbox][water]") {
    SandboxTests::Playground game;
    Water& water = Pond(game);
    const Spot::Vector3 middle = water.GetEntity().GetTransform().GetWorldPosition();
    CHECK(Water::FindAt(game.scene, middle + Spot::Vector3{0, 5, 0}) == &water);
    CHECK(Water::FindAt(game.scene, middle + Spot::Vector3{500, 0, 0}) == nullptr);
    CHECK(water.Contains(middle - Spot::Vector3{0, 1, 0}));
    CHECK_FALSE(water.Contains(middle + Spot::Vector3{0, 1, 0}));

    const auto hit = Gameplay::Traces::Raycast(game.scene, middle + Spot::Vector3{0, 10, 0}, {0, -1, 0}, 100);
    REQUIRE(hit);
    CHECK(hit->volume == &water);
    CHECK(hit->distance == Catch::Approx(10));
    CHECK(hit->point.y == Catch::Approx(water.GetHeight()));
    // Not from below, nor short of it.
    CHECK_FALSE(Gameplay::Traces::Raycast(game.scene, middle - Spot::Vector3{0, 1, 0}, {0, -1, 0}, 100));
    CHECK_FALSE(Gameplay::Traces::Raycast(game.scene, middle + Spot::Vector3{0, 10, 0}, {0, -1, 0}, 5));
}

TEST_CASE("Shots splash the pond, and the water stops them", "[sandbox][water]") {
    SandboxTests::Playground game;
    RunFor(game, 1.5f);
    auto& scene = game.scene;
    Water& water = Pond(game);
    auto& holder = *Player(game).GetComponent<Gameplay::Inventory>();
    holder.Select(1);
    RunFor(game, 0.5f);
    REQUIRE(SandboxTests::GetFirearm(holder));
    const Spot::Vector3 target = AimAtPond(game, water);
    const Spot::Vector3 aim = Player(game).GetComponent<Gameplay::FirstPersonView>()->GetAimDirection();
    // Deeper along the shot than the water lets it go.
    Spot::Entity& deep = Crate(game.scene, target + aim * (water.shotDepth + 1.5f), 5);
    deep.GetComponent<Spot::Rigidbody>()->SetGravityScale(0);
    SandboxTests::Fire(holder);
    CHECK(ParticleCount(game.scene, Layer(scene, "splash", 0)) > 0);
    CHECK(ParticleCount(game.scene, Layer(scene, "splash.ripples")) > 0);
    // What hits the water raises no dust under it.
    CHECK(ParticleCount(game.scene, Layer(scene, "impact.concrete")) == 0);
    game.Step();
    CHECK(Spot::Length(deep.GetComponent<Spot::Rigidbody>()->GetLinearVelocity()) < 0.01f);
    // The rings spread and fade.
    RunFor(game, 3.0f);
    CHECK(ParticleCount(game.scene, Layer(scene, "splash.ripples")) == 0);
}

TEST_CASE("Things falling in the pond splash, and the water slows them", "[sandbox][water]") {
    SandboxTests::Playground game;
    RunFor(game, 1.5f);
    auto& scene = game.scene;
    Water& water = Pond(game);
    const Spot::Vector3 middle = PondMiddle(water);
    constexpr float Height = 3.0f;
    Spot::Entity& crate = Crate(game.scene, middle + Spot::Vector3{0, Height, 0}, 20);
    auto& body = *crate.GetComponent<Spot::Rigidbody>();
    int steps = 0;
    while (!water.Contains(crate.GetTransform().GetWorldPosition()) && steps++ < 200)
        game.Step();
    REQUIRE(water.Contains(crate.GetTransform().GetWorldPosition()));
    // The water looks for what came in on its next step.
    game.Run(2);
    CHECK(ParticleCount(game.scene, Layer(scene, "splash", 0)) > 0);
    CHECK(ParticleCount(game.scene, Layer(scene, "splash.mist")) > 0);
    // A while under, it falls slower than it would through the air.
    RunFor(game, 0.15f);
    const float fallen = Height + middle.y - crate.GetTransform().GetWorldPosition().y + 0.3f;
    const float freeFall = std::sqrt(2 * 9.81f * fallen);
    CHECK(-body.GetLinearVelocity().y < freeFall * 0.95f);
}

TEST_CASE("Rockets explode on the water and throw up a plume of it", "[sandbox][water]") {
    SandboxTests::Playground game;
    RunFor(game, 1.5f);
    auto& scene = game.scene;
    Water& water = Pond(game);
    const Spot::Vector3 middle = water.GetEntity().GetTransform().GetWorldPosition();
    // The rocket the player's launcher fires.
    Spot::AssetRef<Spot::Prefab> rocket;
    for (const Gameplay::HeldItem* item : Player(game).GetComponent<Gameplay::Inventory>()->GetItems()) {
        if (const auto* firearm = dynamic_cast<const Gameplay::Firearm*>(item); firearm && firearm->projectile) {
            rocket = firearm->projectile;
        }
    }
    REQUIRE(rocket);
    auto* launched = Gameplay::Projectile::Launch(game.scene, rocket, middle + Spot::Vector3{0, 4, 0}, {0, -1, 0}, 30,
                                                  Spot::EntityId::Invalid);
    REQUIRE(launched);
    const auto rocketId = launched->GetEntity().GetId();
    RunFor(game, 0.3f);
    CHECK(game.scene.GetEntity(rocketId) == nullptr);
    CHECK(ParticleCount(game.scene, Layer(scene, "splash", 1)) > Layer(scene, "splash", 1).count);
    CHECK(ParticleCount(game.scene, Layer(scene, "splash.plume")) > 0);
    // The water, not the pond's bottom, takes the blast: it does not burn, and raises no dust.
    CHECK(ParticleCount(game.scene, Layer(scene, "explosion", 1)) == 0);
    CHECK(ParticleCount(game.scene, Layer(scene, "explosion.dust")) == 0);

    // Under the water, there is no fire.
    RunFor(game, 6.0f);
    Gameplay::Explosive::Explode(game.scene, middle - Spot::Vector3{0, 0.6f, 0},
                                 {.radius = 3.0f, .impulse = 0.0f, .damage = 0.0f});
    CHECK(ParticleCount(game.scene, Layer(scene, "explosion", 1)) == 0);
    CHECK(ParticleCount(game.scene, Layer(scene, "splash", 1)) > 0);
}
