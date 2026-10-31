#include "gameplay/core/players.h"
#include "gameplay/game/game_mode.h"
#include "interface_lookup.h"
#include "playground_fixture.h"
#include "tool_gun.h"

#include "gameplay/camera/first_person_view.h"
#include "gameplay/character/character.h"
#include "gameplay/character/character_movement.h"
#include "gameplay/health/health.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/inventory/view_model.h"
#include "gameplay/inventory/weapon_wheel.h"
#include "gameplay/weapons/explosive.h"
#include "gameplay/weapons/firearm.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using SandboxTests::Playground;

namespace {

struct Player {
    Playground& game;
    Gameplay::Inventory& holder = *Gameplay::Players::FindComponent<Gameplay::Inventory>(game.scene);
    Gameplay::CharacterMovement& movement = *Gameplay::Players::FindComponent<Gameplay::CharacterMovement>(game.scene);
    Gameplay::Health& health = *Gameplay::Players::FindComponent<Gameplay::Health>(game.scene);
    Gameplay::ViewModel& view = *Gameplay::Players::FindComponent<Gameplay::ViewModel>(game.scene);
    Gameplay::FirstPersonView& controller = *Gameplay::Players::FindComponent<Gameplay::FirstPersonView>(game.scene);

    void Press(Spot::Key key) { game.input.OnEvent(Spot::KeyPressedEvent{key, false}); }
    void Release(Spot::Key key) { game.input.OnEvent(Spot::KeyReleasedEvent{key}); }
    void Tap(Spot::Key key) {
        Press(key);
        game.Step();
        Release(key);
        game.Step();
    }
    Spot::Vector3 GetPosition() const { return holder.GetEntity().GetTransform().GetWorldPosition(); }
    // Puts the player in the air at the height above where it is, falling from rest.
    void Lift(float height) {
        holder.GetEntity().GetTransform().SetPosition(GetPosition() + Spot::Vector3{0.0f, height, 0.0f});
        game.Step();
    }
    // Steps until the player stands on the ground again, for at most the seconds.
    void Land(float seconds = 6.0f) {
        const auto* body = holder.GetEntity().GetComponent<Spot::CharacterController>();
        for (int frame = 0; frame < static_cast<int>(seconds * 60.0f); ++frame) {
            game.Step();
            if (body->IsGrounded() && frame > 2) {
                return;
            }
        }
    }
};

const Spot::Entity* Find(Playground& game, Spot::EntityId id) {
    return game.scene.GetEntity(id);
}

} // namespace

TEST_CASE("Running forward lowers what is in hand, which cannot fire until the run ends", "[sandbox][player]") {
    Playground game;
    Player player{game};
    player.Tap(Spot::Key::Digit2);
    Gameplay::Firearm* pistol = SandboxTests::GetFirearm(player.holder);
    REQUIRE(pistol);
    game.Run(30);

    player.Press(Spot::Key::W);
    player.Press(Spot::Key::LeftShift);
    game.Run(40);
    CHECK(player.movement.IsSprinting());
    CHECK(player.view.GetSprint() > 0.9f);
    CHECK_FALSE(player.holder.CanUseItem());
    const int rounds = pistol->GetAmmo();
    SandboxTests::Fire(player.holder);
    game.input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left});
    game.Run(2);
    game.input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left});
    CHECK(pistol->GetAmmo() == rounds);

    // Running backward is walking.
    player.Release(Spot::Key::W);
    player.Press(Spot::Key::S);
    game.Run(10);
    CHECK_FALSE(player.movement.IsSprinting());
    player.Release(Spot::Key::S);

    // Aiming takes the run away, and the weapon comes back up.
    player.Press(Spot::Key::W);
    game.Run(20);
    REQUIRE(player.movement.IsSprinting());
    game.input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Right});
    game.Run(30);
    CHECK(pistol->IsAiming());
    CHECK_FALSE(player.movement.IsSprinting());
    CHECK(player.view.GetSprint() < 0.1f);
    game.input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Right});
    player.Release(Spot::Key::LeftShift);
    player.Release(Spot::Key::W);
    game.Run(30);
    CHECK(player.holder.CanUseItem());
    SandboxTests::Fire(player.holder);
    CHECK(pistol->GetAmmo() == rounds - 1);
}

TEST_CASE("Holding Tab opens the weapon wheel: the mouse points at a slot and letting go takes it",
          "[sandbox][player][ui]") {
    Playground game;
    Player player{game};
    auto ui = SandboxTests::Interface{game.scene};
    REQUIRE(Spot::Input::IsCursorLocked());
    REQUIRE(player.holder.GetItemCount() == 6);
    CHECK_FALSE(Find(game, ui.wheelOverlay)->IsActiveSelf());

    const float yaw = player.controller.GetYaw();
    player.Press(Spot::Key::Tab);
    game.Step();
    CHECK(player.holder.IsWheelOpen());
    CHECK_FALSE(player.holder.CanUseItem());
    CHECK(Spot::Time::GetTimeScale() == Catch::Approx(player.holder.wheelTimeScale));
    game.Step();
    CHECK(Find(game, ui.wheelOverlay)->IsActiveSelf());
    auto* wheel = Find(game, ui.weaponWheel)->GetComponent<Gameplay::WeaponWheel>();
    REQUIRE(wheel);
    CHECK(wheel->slots.size() == 6);
    // Headless, textures do not load, but every slot names its icon.
    for (const Gameplay::WeaponWheel::Slot& slot : wheel->slots) {
        CHECK(slot.icon.GetId().IsValid());
    }

    // Right of the middle is the third slot, a quarter turn clockwise from the top; the view does not turn meanwhile.
    game.input.OnEvent(Spot::MouseMovedEvent{{700, 360}, {80, 0}});
    game.Step();
    CHECK(player.holder.GetWheelSlot() == 2);
    CHECK(player.controller.GetYaw() == Catch::Approx(yaw));
    game.Step();
    CHECK(wheel->highlighted == 2);
    // Down and to the left, as the screen counts down: the sixth.
    game.input.OnEvent(Spot::MouseMovedEvent{{600, 400}, {-160, 160}});
    game.Step();
    CHECK(player.holder.GetWheelSlot() == 5);
    // Left, the seventh sector is empty, and keeps what was pointed at.
    game.input.OnEvent(Spot::MouseMovedEvent{{600, 400}, {-100, -80}});
    game.Step();
    CHECK(player.holder.GetWheelSlot() == 5);

    player.Release(Spot::Key::Tab);
    game.Run(2);
    CHECK_FALSE(player.holder.IsWheelOpen());
    CHECK(SandboxTests::IsEquipped<ToolGun>(player.holder));
    CHECK(Spot::Time::GetTimeScale() == Catch::Approx(1.0f));
    CHECK_FALSE(Find(game, ui.wheelOverlay)->IsActiveSelf());

    // The number keys still switch at once.
    player.Tap(Spot::Key::Digit4);
    CHECK(player.holder.GetSelected() == 3);
    CHECK(Find(game, ui.itemIcon)->GetComponent<Spot::UiImage>()->texture.GetId() ==
          SandboxTests::GetFirearm(player.holder)->icon.GetId());

    // Pausing over the wheel closes it without choosing.
    player.Press(Spot::Key::Tab);
    game.Step();
    game.input.OnEvent(Spot::MouseMovedEvent{{700, 360}, {0, 100}});
    game.Step();
    ui.pause.Open();
    game.Step();
    CHECK_FALSE(player.holder.IsWheelOpen());
    CHECK(player.holder.GetSelected() == 3);
    ui.pause.Close();
    player.Release(Spot::Key::Tab);
    game.Step();
    CHECK(Spot::Time::GetTimeScale() == Catch::Approx(1.0f));
}

TEST_CASE("Long falls and explosions hurt the player, who heals, dies and comes back", "[sandbox][player]") {
    Playground game;
    Player player{game};
    auto ui = SandboxTests::Interface{game.scene};
    const auto text = [&](Spot::EntityId id) { return Find(game, id)->GetComponent<Spot::UiText>()->text; };
    REQUIRE(player.health.GetHealth() == player.health.maxHealth);
    CHECK(text(ui.healthText) == "100");
    const Spot::Vector3 start = player.GetPosition();

    // A few meters are safe; thirty are not, though the player lives.
    player.Lift(5.0f);
    player.Land();
    CHECK(player.health.GetHealth() == player.health.maxHealth);
    player.Lift(30.0f);
    player.Land();
    const float afterFall = player.health.GetHealth();
    CHECK(afterFall < 50.0f);
    CHECK_FALSE(player.health.IsDead());
    game.Step();
    CHECK(text(ui.healthText) == std::to_string(static_cast<int>(std::ceil(afterFall))));
    CHECK(Find(game, ui.damageOverlay)->IsActiveSelf());

    // Health comes back a few seconds after the last hurt.
    game.Run(static_cast<int>((player.health.regenerationDelay + 1.0f) * 60.0f));
    CHECK(player.health.GetHealth() > afterFall);
    game.Run(15 * 60);
    CHECK(player.health.GetHealth() == player.health.maxHealth);

    // An explosion two meters in front hurts and throws the player back.
    const auto* body = player.holder.GetEntity().GetComponent<Spot::CharacterController>();
    const Spot::Vector3 forward = player.controller.GetAimDirection();
    const Spot::Vector3 flat = Spot::Normalize(Spot::Vector3{forward.x, 0.0f, forward.z});
    const Spot::Vector3 before = player.GetPosition();
    Gameplay::Explosive::Explode(game.scene, before + flat * 2.0f,
                                 {.radius = 5.0f, .impulse = 500.0f, .damage = 100.0f});
    CHECK(player.health.GetHealth() < player.health.maxHealth - 30.0f);
    CHECK_FALSE(player.health.IsDead());
    game.Run(20);
    CHECK(Spot::Dot(player.GetPosition() - before, flat) < -0.5f);
    CHECK(body->IsEnabled());
    // Far ones do not.
    const float health = player.health.GetHealth();
    Gameplay::Explosive::Explode(game.scene, before + flat * 40.0f,
                                 {.radius = 5.0f, .impulse = 500.0f, .damage = 100.0f});
    CHECK(player.health.GetHealth() == health);

    // Dead, the player drops what is in hand, sees how long until it comes back, and comes back at the start.
    player.health.TakeDamage({.amount = 1000.0f});
    CHECK(player.health.IsDead());
    CHECK_FALSE(player.holder.CanUseItem());
    game.Run(2);
    CHECK(Find(game, ui.deathOverlay)->IsActiveSelf());
    CHECK(text(ui.deathTimer).starts_with("Respawning in "));
    CHECK(player.view.GetShownModel().Get() == nullptr);
    CHECK(player.health.TakeDamage({.amount = 10.0f}) == 0.0f);
    game.Run(static_cast<int>((Gameplay::GameMode::Get(game.scene)->respawnTime + 0.5f) * 60.0f));
    CHECK_FALSE(player.health.IsDead());
    CHECK(player.health.GetHealth() == player.health.maxHealth);
    CHECK(Spot::Distance(player.GetPosition(), start) < 1.0f);
    CHECK_FALSE(Find(game, ui.deathOverlay)->IsActiveSelf());
}

TEST_CASE("First-person hands follow every held item and disappear with the view model", "[sandbox][player][arms]") {
    Playground game;
    Player player{game};
    REQUIRE(player.view.armsModel);
    for (int slot = 0; slot < player.holder.GetItemCount(); ++slot) {
        player.holder.Select(slot);
        game.Run(30);
        REQUIRE(player.view.GetArmEntities().size() == 4);
        for (auto id : player.view.GetArmEntities()) {
            const auto* arm = game.scene.GetEntity(id);
            REQUIRE(arm);
            REQUIRE(arm->GetComponent<Spot::MeshRenderer>());
            const auto position = arm->GetTransform().GetWorldPosition();
            CHECK(Spot::Length(position - player.controller.GetEyePosition()) < 1.5f);
            CHECK_FALSE(arm->GetComponent<Spot::MeshRenderer>()->IsCastingShadows());
        }
    }
    player.holder.Select(1);
    game.Run(30);
    const auto ids = player.view.GetArmEntities();
    const auto before = game.scene.GetEntity(ids[1])->GetTransform().GetWorldPosition();
    player.view.Kick();
    game.Run(2);
    CHECK(Spot::Length(game.scene.GetEntity(ids[1])->GetTransform().GetWorldPosition() - before) > 0.005f);
    player.controller.SetLook(0.8f, 0.4f);
    game.Run(2);
    CHECK(Spot::Length(game.scene.GetEntity(ids[1])->GetTransform().GetWorldPosition() - before) > 0.1f);
    auto* pistol = SandboxTests::GetFirearm(player.holder);
    REQUIRE(pistol);
    pistol->Fire();
    REQUIRE(pistol->StartReload());
    const auto gripDistance = [&] {
        return Spot::Length(game.scene.GetEntity(ids[3])->GetTransform().GetWorldPosition() -
                            game.scene.GetEntity(ids[1])->GetTransform().GetWorldPosition());
    };
    const float holding = gripDistance();
    for (int frame = 0; frame < 120 && pistol->GetReloadProgress() < 0.45f; ++frame) {
        game.Step();
    }
    CHECK(gripDistance() > holding + 0.10f);
    auto* character = player.holder.GetEntity().GetComponent<Gameplay::Character>();
    REQUIRE(character);
    REQUIRE(character->SetThirdPerson(true));
    game.Step();
    CHECK(player.view.GetArmEntities().empty());
    for (auto id : ids) {
        CHECK_FALSE(game.scene.GetEntity(id));
    }
    REQUIRE(character->SetThirdPerson(false));
    game.Run(30);
    CHECK(player.view.GetArmEntities().size() == 4);
    player.view.SetEnabled(false);
    game.Step();
    CHECK(player.view.GetArmEntities().empty());
}
