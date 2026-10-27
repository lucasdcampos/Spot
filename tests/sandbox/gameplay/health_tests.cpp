#include "gameplay_world.h"

#include "gameplay/core/pawn.h"
#include "gameplay/health/damage.h"
#include "gameplay/health/fall_damage.h"
#include "gameplay/health/health.h"
#include "gameplay/health/impact_damage.h"

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

using Catch::Approx;

namespace {

// Remembers what damage reached it, as a game's own receiver would.
class Recorder final : public Spot::Component, public Gameplay::DamageReceiver {
public:
    std::vector<Gameplay::DamageInfo> taken;
    void OnDamage(const Gameplay::DamageInfo& damage) override { taken.push_back(damage); }
};

class ContactCounter final : public Spot::Component, public Spot::CollisionListener {
public:
    int count = 0;
    void OnCollisionEnter(const Spot::Collision&) override { ++count; }
};

// Health with armor: a game changing how damage hurts, which no source notices.
class ArmoredHealth final : public Gameplay::Health {
protected:
    float AdjustDamage(const Gameplay::DamageInfo& damage) const override {
        return damage.type == "Bullet" ? damage.amount * 0.5f : damage.amount;
    }
};

Spot::Entity& AddBody(Spot::Scene& scene, std::string name, Spot::Vector3 position) {
    auto& entity = scene.CreateEntity(std::move(name));
    entity.GetTransform().SetPosition(position);
    entity.AddComponent<Spot::BoxCollider>(Spot::Vector3{1.0f, 1.0f, 1.0f});
    entity.AddComponent<Spot::Rigidbody>().SetGravityScale(0.0f);
    return entity;
}

} // namespace

TEST_CASE("Health takes damage, regenerates and stays dead until restored", "[sandbox][gameplay]") {
    GameplayTests::World world;
    auto& entity = world.scene.CreateEntity("Target");
    auto& pawn = entity.AddComponent<Gameplay::Pawn>();
    auto& health = entity.AddComponent<Gameplay::Health>();
    health.regenerationDelay = 1.0f;
    int died = 0;
    int respawned = 0;
    float hurt = 0.0f;
    auto onDamaged = health.OnDamaged.Connect([&](const Gameplay::DamageInfo&, float taken) { hurt += taken; });
    auto onDied = health.OnDied.Connect([&](const Gameplay::DamageInfo&) { ++died; });
    auto onRespawned = health.OnRestored.Connect([&] { ++respawned; });
    world.Step();

    CHECK(Gameplay::Damage::Apply(world.scene, entity.GetId(), {.amount = 30.0f, .type = "Bullet"}));
    CHECK(health.GetHealth() == Approx(70.0f));
    CHECK(hurt == Approx(30.0f));
    CHECK(health.GetHurt() > 0.0f);
    // It heals a while after the last hurt.
    world.Run(3.0f);
    CHECK(health.GetHealth() > 70.0f);

    // What is left limits what is taken.
    const float left = health.GetHealth();
    CHECK(health.TakeDamage({.amount = 1000.0f}) == Approx(left));
    CHECK(health.IsDead());
    CHECK(pawn.HasState("Dead"));
    CHECK(died == 1);
    // Nothing more is taken once dead.
    CHECK(health.TakeDamage({.amount = 10.0f}) == 0.0f);
    world.Run(6.0f);
    CHECK(health.IsDead());
    CHECK(respawned == 0);
    health.Restore();
    CHECK_FALSE(health.IsDead());
    CHECK_FALSE(pawn.HasState("Dead"));
    CHECK(health.GetHealth() == Approx(health.maxHealth));
    CHECK(respawned == 1);
}

TEST_CASE("A subclass of Health changes how damage hurts", "[sandbox][gameplay]") {
    GameplayTests::World world;
    auto& entity = world.scene.CreateEntity("Armored");
    auto& health = entity.AddComponent<ArmoredHealth>();
    world.Step();
    Gameplay::Damage::Apply(world.scene, entity.GetId(), {.amount = 40.0f, .type = "Bullet"});
    CHECK(health.GetHealth() == Approx(80.0f));
    Gameplay::Damage::Apply(world.scene, entity.GetId(), {.amount = 40.0f, .type = "Explosion"});
    CHECK(health.GetHealth() == Approx(40.0f));
}

TEST_CASE("Damage finds receivers above what it hits, pushes bodies and tells who listens", "[sandbox][gameplay]") {
    GameplayTests::World world;
    auto& root = AddBody(world.scene, "Root", {0.0f, 2.0f, 0.0f});
    auto& recorder = root.AddComponent<Recorder>();
    auto& part = root.CreateChild("Part");
    auto& crate = AddBody(world.scene, "Crate", {5.0f, 2.0f, 0.0f});
    std::vector<Spot::EntityId> applied;
    auto onApplied =
        Gameplay::DamageSystem::Get(world.scene).OnDamageApplied.Connect([&](const Gameplay::DamageApplied& damage) {
            applied.push_back(damage.target);
        });
    world.Step();

    CHECK(Gameplay::Damage::Apply(world.scene, part.GetId(), {.amount = 5.0f, .type = "Bullet"}));
    REQUIRE(recorder.taken.size() == 1);
    CHECK(recorder.taken[0].type == "Bullet");
    // A body with no receiver is still pushed.
    CHECK(Gameplay::Damage::Apply(world.scene, crate.GetId(),
                                  {.point = {5.0f, 2.0f, 0.0f}, .impulse = {0.0f, 0.0f, -10.0f}}));
    CHECK(crate.GetComponent<Spot::Rigidbody>()->GetLinearVelocity().z < 0.0f);
    CHECK(applied == std::vector<Spot::EntityId>{part.GetId(), crate.GetId()});
    // Nothing takes damage given to the ground.
    CHECK_FALSE(Gameplay::Damage::Apply(world.scene, world.ground.GetId(), {.amount = 5.0f}));
}

TEST_CASE("Radial damage reaches each receiver once, less farther, and pushes bodies away", "[sandbox][gameplay]") {
    GameplayTests::World world;
    auto& nearBody = AddBody(world.scene, "Near", {1.5f, 2.0f, 0.0f});
    auto& nearRecorder = nearBody.AddComponent<Recorder>();
    auto& farBody = AddBody(world.scene, "Far", {4.0f, 2.0f, 0.0f});
    auto& farRecorder = farBody.AddComponent<Recorder>();
    auto& outside = AddBody(world.scene, "Outside", {20.0f, 2.0f, 0.0f});
    auto& outsideRecorder = outside.AddComponent<Recorder>();
    world.Step();

    Gameplay::Damage::ApplyRadial(world.scene, {0.0f, 2.0f, 0.0f},
                                  {.radius = 6.0f, .amount = 100.0f, .impulse = 50.0f});
    REQUIRE(nearRecorder.taken.size() == 1);
    REQUIRE(farRecorder.taken.size() == 1);
    CHECK(outsideRecorder.taken.empty());
    CHECK(nearRecorder.taken[0].amount > farRecorder.taken[0].amount);
    CHECK(nearRecorder.taken[0].type == "Explosion");
    CHECK(nearBody.GetComponent<Spot::Rigidbody>()->GetLinearVelocity().x > 0.0f);
}

TEST_CASE("Falls and impacts hurt by speed", "[sandbox][gameplay]") {
    GameplayTests::World world;
    auto& faller = world.scene.CreateEntity("Faller");
    auto& health = faller.AddComponent<Gameplay::Health>();
    auto& fall = faller.AddComponent<Gameplay::FallDamage>();
    world.Step();
    CHECK(fall.Land(fall.safeFallSpeed) == 0.0f);
    CHECK(fall.Land((fall.safeFallSpeed + fall.lethalFallSpeed) * 0.5f) == Approx(health.maxHealth * 0.5f));
    fall.Land(fall.lethalFallSpeed);
    CHECK(health.IsDead());

    // A body running into another fast damages it; a slow one does not.
    auto& target = AddBody(world.scene, "Target", {0.0f, 0.5f, -5.0f});
    auto& recorder = target.AddComponent<Recorder>();
    auto& ram = AddBody(world.scene, "Ram", {0.0f, 0.5f, 0.0f});
    ram.AddComponent<Gameplay::ImpactDamage>();
    ram.GetComponent<Spot::Rigidbody>()->SetLinearVelocity({0.0f, 0.0f, -10.0f});
    world.Run(1.0f);
    REQUIRE_FALSE(recorder.taken.empty());
    CHECK(recorder.taken[0].type == "Impact");
    CHECK(recorder.taken[0].amount > 20.0f);
    CHECK(recorder.taken[0].source == ram.GetId());
}

TEST_CASE("Impact damage requires its source to approach the target fast enough", "[sandbox][gameplay][impact]") {
    GameplayTests::World world;
    auto& ram = AddBody(world.scene, "Ram", {0.0f, 2.0f, 0.0f});
    ram.AddComponent<Gameplay::ImpactDamage>();
    auto& contacts = ram.AddComponent<ContactCounter>();
    auto& body = *ram.GetComponent<Spot::Rigidbody>();
    body.SetMass(1000.0f);

    SECTION("The source is parked") {
        body.SetLinearVelocity({});
    }
    SECTION("The source approaches below the damage threshold") {
        body.SetLinearVelocity({0.0f, 0.0f, -1.0f});
    }
    SECTION("The source moves away from a faster target") {
        body.SetLinearVelocity({0.0f, 0.0f, 3.0f});
    }

    auto& target = AddBody(world.scene, "Target", {0.0f, 2.0f, -4.0f});
    auto& recorder = target.AddComponent<Recorder>();
    target.GetComponent<Spot::Rigidbody>()->SetLinearVelocity({0.0f, 0.0f, 9.0f});
    world.Run(0.75f);

    REQUIRE(contacts.count > 0);
    CHECK(recorder.taken.empty());
}

TEST_CASE("An impact still hurts when the collision stops its source", "[sandbox][gameplay][impact]") {
    GameplayTests::World world;
    auto& target = world.scene.CreateEntity("Target");
    target.GetTransform().SetPosition({0.0f, 2.0f, -4.0f});
    target.AddComponent<Spot::BoxCollider>();
    auto& recorder = target.AddComponent<Recorder>();
    auto& ram = AddBody(world.scene, "Ram", {0.0f, 2.0f, 0.0f});
    ram.AddComponent<Gameplay::ImpactDamage>();
    auto& body = *ram.GetComponent<Spot::Rigidbody>();
    body.SetLinearVelocity({0.0f, 0.0f, -10.0f});
    world.Run(0.5f);

    REQUIRE(recorder.taken.size() == 1);
    CHECK(recorder.taken[0].amount == Approx(100.0f).margin(5.0f));
    CHECK(std::abs(body.GetLinearVelocity().z) < 0.1f);
}
