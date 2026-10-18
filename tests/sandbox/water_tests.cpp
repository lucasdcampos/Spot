#include "explosion.h"
#include "particle_effects.h"
#include "player_controller.h"
#include "playground_fixture.h"
#include "projectile.h"
#include "water.h"
#include "weapon.h"
#include "weapon_holder.h"

#include <catch2/catch_approx.hpp>

#include <cmath>

namespace {

void RunFor(SandboxTests::Playground& game, float seconds) {
    game.Run(static_cast<int>(std::ceil(seconds / Spot::Scene::DefaultFixedTimeStep)));
}

Spot::Entity& Player(SandboxTests::Playground& game) {
    Spot::Entity* player = game.scene.FindEntity("Player");
    REQUIRE(player);
    return *player;
}

std::size_t ParticleCount(Spot::Scene& scene, const ParticleEffect& effect) {
    std::size_t count = 0;
    scene.ForEachComponent<Spot::ParticleSystem>([&](Spot::ParticleSystem& system) {
        if (system.GetSettings().startColor == effect.settings.startColor &&
            system.GetSettings().facing == effect.settings.facing)
            count += system.GetParticles().size();
    });
    return count;
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
    const auto& controller = *player.GetComponent<PlayerController>();
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

    const auto hit = Water::Raycast(game.scene, middle + Spot::Vector3{0, 10, 0}, {0, -1, 0}, 100);
    REQUIRE(hit);
    CHECK(hit->water == &water);
    CHECK(hit->distance == Catch::Approx(10));
    CHECK(hit->point.y == Catch::Approx(water.GetHeight()));
    // Not from below, nor short of it.
    CHECK_FALSE(Water::Raycast(game.scene, middle - Spot::Vector3{0, 1, 0}, {0, -1, 0}, 100));
    CHECK_FALSE(Water::Raycast(game.scene, middle + Spot::Vector3{0, 10, 0}, {0, -1, 0}, 5));
}

TEST_CASE("Shots splash the pond, and the water stops them", "[sandbox][water]") {
    SandboxTests::Playground game;
    RunFor(game, 1.5f);
    auto* bank = ParticleEffects::Find(game.scene);
    REQUIRE(bank);
    Water& water = Pond(game);
    auto& holder = *Player(game).GetComponent<WeaponHolder>();
    holder.Select(1);
    RunFor(game, 0.5f);
    REQUIRE(holder.GetWeapon());
    const Spot::Vector3 target = AimAtPond(game, water);
    const Spot::Vector3 aim = Player(game).GetComponent<PlayerController>()->GetAimDirection();
    // Deeper along the shot than the water lets it go.
    Spot::Entity& deep = Crate(game.scene, target + aim * (water.shotDepth + 1.5f), 5);
    deep.GetComponent<Spot::Rigidbody>()->SetGravityScale(0);
    holder.Fire();
    CHECK(ParticleCount(game.scene, bank->splashDroplets) > 0);
    CHECK(ParticleCount(game.scene, bank->ripples) > 0);
    // What hits the water raises no dust under it.
    CHECK(ParticleCount(game.scene, bank->concreteDust) == 0);
    game.Step();
    CHECK(Spot::Length(deep.GetComponent<Spot::Rigidbody>()->GetLinearVelocity()) < 0.01f);
    // The rings spread and fade.
    RunFor(game, 3.0f);
    CHECK(ParticleCount(game.scene, bank->ripples) == 0);
}

TEST_CASE("Things falling in the pond splash, and the water slows them", "[sandbox][water]") {
    SandboxTests::Playground game;
    RunFor(game, 1.5f);
    auto* bank = ParticleEffects::Find(game.scene);
    REQUIRE(bank);
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
    CHECK(ParticleCount(game.scene, bank->splashDroplets) > 0);
    CHECK(ParticleCount(game.scene, bank->splashMist) > 0);
    // A while under, it falls slower than it would through the air.
    RunFor(game, 0.15f);
    const float fallen = Height + middle.y - crate.GetTransform().GetWorldPosition().y + 0.3f;
    const float freeFall = std::sqrt(2 * 9.81f * fallen);
    CHECK(-body.GetLinearVelocity().y < freeFall * 0.95f);
}

TEST_CASE("Rockets explode on the water and throw up a plume of it", "[sandbox][water]") {
    SandboxTests::Playground game;
    RunFor(game, 1.5f);
    auto* bank = ParticleEffects::Find(game.scene);
    REQUIRE(bank);
    Water& water = Pond(game);
    const Spot::Vector3 middle = water.GetEntity().GetTransform().GetWorldPosition();
    const auto rocketId =
        Projectile::Launch(game.scene, middle + Spot::Vector3{0, 4, 0}, {0, -1, 0}, 30, Spot::EntityId::Invalid)
            .GetId();
    RunFor(game, 0.3f);
    CHECK(game.scene.GetEntity(rocketId) == nullptr);
    CHECK(ParticleCount(game.scene, bank->splashSpray) > bank->splashSpray.count);
    CHECK(ParticleCount(game.scene, bank->waterPlume) > 0);
    // The water, not the pond's bottom, takes the blast: it does not burn, and raises no dust.
    CHECK(ParticleCount(game.scene, bank->fireball) == 0);
    CHECK(ParticleCount(game.scene, bank->groundDust) == 0);

    // Under the water, there is no fire.
    RunFor(game, 6.0f);
    Explosion::Explode(game.scene, middle - Spot::Vector3{0, 0.6f, 0}, 3, 0, 0);
    CHECK(ParticleCount(game.scene, bank->fireball) == 0);
    CHECK(ParticleCount(game.scene, bank->splashSpray) > 0);
}
