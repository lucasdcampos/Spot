#include "gameplay_world.h"

#include "gameplay/core/pawn.h"
#include "gameplay/health/damage.h"
#include "gameplay/health/health.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/weapons/explosive.h"
#include "gameplay/weapons/firearm.h"
#include "gameplay/weapons/ignite_on_damage.h"
#include "gameplay/weapons/projectile.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

namespace {

class Target final : public Spot::Component, public Gameplay::DamageReceiver {
public:
    std::vector<Gameplay::DamageInfo> hits;
    void OnDamage(const Gameplay::DamageInfo& damage) override { hits.push_back(damage); }
};

struct WeaponWorld : GameplayTests::World {
    Spot::Entity& entity = scene.CreateEntity("Pawn");
    Gameplay::Pawn& pawn = entity.AddComponent<Gameplay::Pawn>();
    Gameplay::Inventory& inventory = entity.AddComponent<Gameplay::Inventory>();
    Gameplay::Firearm& firearm = entity.CreateChild("Firearm").AddComponent<Gameplay::Firearm>();

    WeaponWorld() {
        entity.GetTransform().SetPosition({0.0f, 1.0f, 0.0f});
        firearm.magazineSize = 2;
        firearm.reserveAmmo = 4;
        firearm.reloadTime = 0.25f;
        Step();
    }

    void Use(bool fire = true, bool aim = false, bool reload = false) {
        Gameplay::ItemInput intent;
        intent.active = intent.canUse = true;
        intent.Set("Fire", fire, fire);
        intent.Set("Aim", aim);
        intent.Set("Reload", reload, reload);
        inventory.Use(intent);
    }
};

} // namespace

TEST_CASE("A firearm fires into arbitrary damage receivers from an item intent", "[sandbox][gameplay][weapons]") {
    WeaponWorld world;
    auto& entity = world.scene.CreateEntity("Target");
    entity.GetTransform().SetPosition({0.0f, 1.0f, -5.0f});
    entity.AddComponent<Spot::BoxCollider>();
    auto& target = entity.AddComponent<Target>();
    world.Step();
    world.Use();
    REQUIRE(target.hits.size() == 1);
    CHECK(target.hits.front().type == "Bullet");
    CHECK(target.hits.front().instigator == world.entity.GetId());
    CHECK(target.hits.front().source == world.firearm.GetEntity().GetId());
    CHECK(world.firearm.GetAmmo() == 1);
    // It cannot fire again before the interval, even with another intent.
    world.Use();
    CHECK(target.hits.size() == 1);
}

TEST_CASE("A firearm reloads and clears its aiming state when stowed or switched", "[sandbox][gameplay][weapons]") {
    WeaponWorld world;
    world.Use(false, true);
    CHECK(world.firearm.IsAiming());
    CHECK(world.pawn.HasState("Aiming"));
    CHECK(world.pawn.GetZoom() == world.firearm.aimZoom);
    world.Use();
    world.Use(false, false, true);
    REQUIRE(world.firearm.IsReloading());
    world.Run(0.4f);
    CHECK(world.firearm.GetAmmo() == 2);
    CHECK(world.firearm.GetReserve() == 3);
    world.Use(false, true);
    world.pawn.SetState("Seated", true);
    world.Use(false, true);
    CHECK_FALSE(world.firearm.IsAiming());
    CHECK_FALSE(world.pawn.HasState("Aiming"));
    world.pawn.SetState("Seated", false);
    world.Use(false, true);
    auto& other = world.entity.CreateChild("Other").AddComponent<Gameplay::Firearm>();
    world.Step();
    world.inventory.Select(other);
    CHECK_FALSE(world.pawn.HasState("Aiming"));
}

TEST_CASE("An external item intent cannot fire through inventory states", "[sandbox][gameplay][weapons]") {
    WeaponWorld world;
    world.pawn.SetState("Sprinting", true);
    world.Use();
    CHECK(world.firearm.GetAmmo() == 2);
    world.pawn.SetState("Sprinting", false);
    world.Use();
    CHECK(world.firearm.GetAmmo() == 1);
}

TEST_CASE("Any damaged explosive can ignite a neighboring explosive through damage", "[sandbox][gameplay][weapons]") {
    GameplayTests::World world;
    auto& first = world.scene.CreateEntity("First");
    first.GetTransform().SetPosition({0.0f, 1.0f, 0.0f});
    auto& explosive = first.AddComponent<Gameplay::Explosive>();
    explosive.blast.radius = 4.0f;
    first.AddComponent<Gameplay::IgniteOnDamage>().fuse = 0.1f;
    auto& next = world.scene.CreateEntity("Next");
    next.GetTransform().SetPosition({1.0f, 1.0f, 0.0f});
    next.AddComponent<Spot::BoxCollider>();
    next.AddComponent<Gameplay::Explosive>();
    next.AddComponent<Gameplay::IgniteOnDamage>().fuse = 0.1f;
    const auto firstId = first.GetId();
    const auto nextId = next.GetId();
    world.Step();
    CHECK(Gameplay::Damage::Apply(world.scene, firstId, {.amount = 1.0f, .type = "Bullet"}));
    world.Run(0.15f);
    CHECK(world.scene.GetEntity(firstId) == nullptr);
    REQUIRE(world.scene.GetEntity(nextId));
    CHECK(world.scene.GetEntity(nextId)->GetComponent<Gameplay::IgniteOnDamage>()->IsLit());
    world.Run(0.2f);
    CHECK(world.scene.GetEntity(nextId) == nullptr);
}

TEST_CASE("A projectile prefab launches with its own blast and expires without a firearm",
          "[sandbox][gameplay][weapons]") {
    GameplayTests::World world;
    Spot::Scene source;
    auto& root = source.CreateEntity("Projectile");
    root.AddComponent<Spot::SphereCollider>();
    root.AddComponent<Spot::Rigidbody>().SetGravityScale(0.0f);
    root.AddComponent<Gameplay::Projectile>().lifetime = 0.2f;
    root.AddComponent<Gameplay::Explosive>().blast.radius = 2.0f;
    auto prefab = Spot::Prefab::FromString(root.SaveToString());
    REQUIRE(prefab);
    auto& shooter = world.scene.CreateEntity("Shooter");
    auto* projectile =
        Gameplay::Projectile::Launch(world.scene, std::make_shared<const Spot::Prefab>(std::move(*prefab)),
                                     {0.0f, 3.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 10.0f, shooter.GetId());
    REQUIRE(projectile);
    const auto id = projectile->GetEntity().GetId();
    CHECK(projectile->shooter == shooter.GetId());
    CHECK(projectile->GetEntity().GetComponent<Gameplay::Explosive>()->blast.radius == 2.0f);
    world.Run(0.1f);
    REQUIRE(world.scene.GetEntity(id));
    CHECK(world.scene.GetEntity(id)->GetTransform().GetWorldPosition().z < -0.5f);
    world.Run(0.2f);
    CHECK(world.scene.GetEntity(id) == nullptr);
}

TEST_CASE("Weapon damage balances properly and headshots are lethal", "[sandbox][gameplay][weapons]") {
    GameplayTests::World world;

    auto& pistolEntity = world.scene.CreateEntity("Pistol");
    auto& pistol = pistolEntity.AddComponent<Gameplay::Firearm>();
    pistol.damage = 25.0f;
    pistol.pellets = 1;

    auto& rifleEntity = world.scene.CreateEntity("Rifle");
    auto& rifle = rifleEntity.AddComponent<Gameplay::Firearm>();
    rifle.damage = 35.0f;
    rifle.pellets = 1;

    auto& shotgunEntity = world.scene.CreateEntity("Shotgun");
    auto& shotgun = shotgunEntity.AddComponent<Gameplay::Firearm>();
    shotgun.damage = 16.0f;
    shotgun.pellets = 10;

    // Order of damage per shot: Pistol < Rifle < Shotgun
    CHECK(pistol.damage < rifle.damage);
    CHECK(rifle.damage < shotgun.damage * shotgun.pellets);

    // Target character with 100 max health and a CharacterController capsule
    auto& target = world.scene.CreateEntity("CharacterTarget");
    target.GetTransform().SetPosition({0.0f, 0.9f, 0.0f});
    auto& controller = target.AddComponent<Spot::CharacterController>();
    controller.SetHeight(1.8f);
    controller.SetRadius(0.4f);
    controller.SetCenter({0.0f, 0.0f, 0.0f});
    auto& health = target.AddComponent<Gameplay::Health>();
    health.maxHealth = 100.0f;
    world.Step();

    // Body hit (at torso height y = 1.0f)
    const Spot::Vector3 bodyHit{0.0f, 1.0f, 0.4f};
    CHECK_FALSE(Gameplay::Damage::IsHeadshot(target, bodyHit));

    // Head hit (at head height y = 1.65f)
    const Spot::Vector3 headHit{0.0f, 1.65f, 0.25f};
    CHECK(Gameplay::Damage::IsHeadshot(target, headHit));

    // A shotgun blast to the head deals hitkill damage (instant death in one shot)
    for (int p = 0; p < shotgun.pellets; ++p) {
        Gameplay::Damage::Apply(world.scene, target.GetId(),
                                {.amount = shotgun.damage, .type = "Bullet", .point = headHit});
    }
    CHECK(health.IsDead());
}

TEST_CASE("Close explosions deal increased damage and kill within blast range", "[sandbox][gameplay][weapons]") {
    GameplayTests::World world;

    Gameplay::BlastSettings blast;
    // Default blast settings deal high damage
    CHECK(blast.damage >= 250.0f);
    CHECK(blast.radius >= 6.0f);

    // A close explosion (1.5m away) on a 100 HP character is an instant hitkill
    auto& target = world.scene.CreateEntity("BlastTarget");
    target.GetTransform().SetPosition({1.5f, 0.9f, 0.0f});
    auto& controller = target.AddComponent<Spot::CharacterController>();
    controller.SetHeight(1.8f);
    controller.SetRadius(0.4f);
    auto& health = target.AddComponent<Gameplay::Health>();
    health.maxHealth = 100.0f;
    world.Step();

    Gameplay::Damage::ApplyRadial(world.scene, {0.0f, 0.9f, 0.0f},
                                  {.radius = blast.radius, .amount = blast.damage, .impulse = blast.impulse});
    CHECK(health.IsDead());
}
