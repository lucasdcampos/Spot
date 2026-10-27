#include "gameplay_world.h"

#include "gameplay/core/input_context.h"
#include "gameplay/core/interactable.h"
#include "gameplay/core/interactor.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/core/player_input.h"
#include "gameplay/core/players.h"
#include "gameplay/core/user_settings.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct CoreWorld : GameplayTests::World {
    CoreWorld() {
        actions.Add("Use", Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::E));
        actions.Add("Fire", Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::F));
    }

    void Press(Spot::Key key) { input.OnEvent(Spot::KeyPressedEvent{key, false}); }
    void Release(Spot::Key key) { input.OnEvent(Spot::KeyReleasedEvent{key}); }
    // Reads this frame's input into the actions without updating the scene, so contexts can be asked about it.
    void Poll() { actions.Update(input); }

    Gameplay::PlayerController& AddPlayer(Spot::Entity* pawn = nullptr) {
        auto& entity = scene.CreateEntity("Player Controller");
        auto& player = entity.AddComponent<Gameplay::PlayerController>();
        player.pawn = pawn ? pawn->GetId() : Spot::EntityId::Invalid;
        return player;
    }

    // A pawn standing at the origin with its eye a meter up, looking along -Z, and a camera there, off.
    Gameplay::Pawn& AddPawn(std::string name) {
        auto& entity = scene.CreateEntity(std::move(name));
        auto& eye = entity.CreateChild("Eye");
        eye.GetTransform().SetPosition({0.0f, 1.0f, 0.0f});
        eye.AddComponent<Spot::Camera>().SetEnabled(false);
        auto& pawn = entity.AddComponent<Gameplay::Pawn>();
        pawn.eye = eye.GetId();
        pawn.viewCamera = eye.GetId();
        return pawn;
    }

    static bool IsCameraOn(const Gameplay::Pawn& pawn) {
        const Spot::Entity* eye = pawn.GetScene().GetEntity(pawn.viewCamera);
        return eye && eye->GetComponent<Spot::Camera>()->IsEnabled();
    }
};

class Door final : public Gameplay::Interactable {
public:
    int opened = 0;
    void Interact(Gameplay::Interactor& /*interactor*/) override { ++opened; }
};

// Picks up whatever has a rigidbody, as the sandbox's carrier does.
class Grabber final : public Spot::Component, public Gameplay::InteractionHandler {
public:
    Spot::EntityId grabbed = Spot::EntityId::Invalid;

    std::optional<std::string> GetInteraction(const Gameplay::Interactor& /*interactor*/,
                                              const Spot::RaycastHit& hit) const override {
        const Spot::Entity* entity = GetScene().GetEntity(hit.entity);
        return entity && entity->HasComponent<Spot::Rigidbody>() ? std::optional<std::string>("Grab") : std::nullopt;
    }
    void Interact(Gameplay::Interactor& /*interactor*/, const Spot::RaycastHit& hit) override { grabbed = hit.entity; }
};

// Reads Use below the interactor, after it, as another part of the pawn would.
class UseReader final : public Spot::Component {
public:
    int presses = 0;

protected:
    void OnUpdate(float /*deltaTime*/) override {
        m_input.Activate(Gameplay::Pawn::GetControllerOf(GetEntity()));
        presses += m_input.IsPressed("Use") ? 1 : 0;
    }

private:
    Gameplay::InputContext m_input{{.name = "Reader", .actions = {"Use"}}};
};

class SettingsPart final : public Spot::Component, public Gameplay::UserSettingsListener {
public:
    int applied = 0;
    float sensitivity = 0.0f;
    void ApplySettings(const Gameplay::UserSettings& settings) override {
        ++applied;
        sensitivity = settings.sensitivity;
    }
};

Spot::Entity& AddBox(Spot::Scene& scene, Spot::Vector3 position) {
    auto& box = scene.CreateEntity("Box");
    box.GetTransform().SetPosition(position);
    box.AddComponent<Spot::BoxCollider>(Spot::Vector3{1.0f, 1.0f, 1.0f});
    return box;
}

} // namespace

TEST_CASE("A controller possesses one pawn at a time and shows its camera", "[sandbox][gameplay]") {
    CoreWorld world;
    auto& walker = world.AddPawn("Walker");
    auto& car = world.AddPawn("Car");
    auto& player = world.AddPlayer(&walker.GetEntity());
    Gameplay::Controller* lastPossessor = nullptr;
    int possessed = 0;
    int unpossessed = 0;
    auto onPossessed = car.OnPossessed.Connect([&](Gameplay::Controller& by) {
        CHECK(car.GetController() == &by);
        lastPossessor = &by;
        ++possessed;
    });
    auto onUnpossessed = walker.OnUnpossessed.Connect([&] { ++unpossessed; });

    // It possesses its pawn as it starts.
    CHECK_FALSE(walker.IsPossessed());
    world.Step();
    CHECK(player.GetPawn() == &walker);
    CHECK(walker.GetController() == &player);
    CHECK(CoreWorld::IsCameraOn(walker));
    CHECK_FALSE(CoreWorld::IsCameraOn(car));
    CHECK(Gameplay::Players::Get(world.scene) == &player);
    CHECK(Gameplay::Players::GetPawn(world.scene) == &walker);
    CHECK(Gameplay::Players::Get(world.scene, 1) == nullptr);

    // Getting into the car is the controller possessing it: the walker's camera goes off, the car's comes on.
    player.Possess(car);
    CHECK(player.GetPawn() == &car);
    CHECK_FALSE(walker.IsPossessed());
    CHECK(car.GetController() == &player);
    CHECK_FALSE(CoreWorld::IsCameraOn(walker));
    CHECK(CoreWorld::IsCameraOn(car));
    CHECK(possessed == 1);
    CHECK(lastPossessor == &player);
    CHECK(unpossessed == 1);
    // Possessing it again changes nothing.
    player.Possess(car);
    CHECK(possessed == 1);

    // Another controller takes it from the first.
    auto& other = world.AddPlayer();
    world.Step();
    other.Possess(car);
    CHECK(possessed == 2);
    CHECK(lastPossessor == &other);
    CHECK(car.GetController() == &other);
    CHECK(player.GetPawn() == nullptr);
    CHECK(Gameplay::Players::GetAll(world.scene).size() == 2);

    // A pawn gone leaves its controller with none.
    car.GetEntity().Destroy();
    world.Step();
    CHECK(other.GetPawn() == nullptr);
}

TEST_CASE("A pawn's states are names anyone sets and reads", "[sandbox][gameplay]") {
    CoreWorld world;
    auto& pawn = world.AddPawn("Pawn");
    pawn.SetState("Aiming", true);
    pawn.SetState("Aiming", true);
    pawn.SetState("Dead", true);
    CHECK(pawn.HasState("Aiming"));
    CHECK(pawn.GetStates().size() == 2);
    pawn.SetState("Aiming", false);
    CHECK_FALSE(pawn.HasState("Aiming"));
    const std::vector<std::string> blocking = {"Seated", "Dead"};
    CHECK(pawn.HasAnyState(blocking));
    pawn.SetState("Dead", false);
    CHECK_FALSE(pawn.HasAnyState(blocking));
    // A child entity finds the pawn above it.
    CHECK(Gameplay::Pawn::Of(*pawn.GetEntity().FindChild("Eye")) == &pawn);
}

TEST_CASE("Input contexts take their actions from the contexts below them", "[sandbox][gameplay]") {
    CoreWorld world;
    auto& pawn = world.AddPawn("Pawn");
    auto& player = world.AddPlayer(&pawn.GetEntity());
    world.Step();

    Gameplay::InputContext weapon{{.name = "Weapon", .priority = 10, .actions = {"Fire", "Use"}}};
    Gameplay::InputContext carry{{.name = "Carry", .priority = 30, .actions = {"Fire"}}};
    Gameplay::InputContext same{{.name = "Same", .priority = 10, .actions = {"Fire"}}};

    // Without a controller a context sees nothing, as a pawn nobody possesses.
    weapon.Activate(nullptr);
    world.Press(Spot::Key::F);
    world.Poll();
    CHECK_FALSE(weapon.IsActive());
    CHECK_FALSE(weapon.IsDown("Fire"));

    weapon.Activate(pawn.GetController());
    CHECK(weapon.IsActive());
    CHECK(weapon.IsPressed("Fire"));
    CHECK(weapon.IsDown("Fire"));
    // Only the actions it takes.
    CHECK_FALSE(weapon.Takes("Jump"));
    CHECK_FALSE(weapon.IsAvailable("Jump"));

    // A context above takes Fire; Use, which it does not take, still reaches the one below.
    carry.Activate(&player);
    CHECK(carry.IsPressed("Fire"));
    CHECK_FALSE(weapon.IsPressed("Fire"));
    CHECK_FALSE(weapon.IsDown("Fire"));
    CHECK(weapon.IsAvailable("Use"));
    same.Activate(&player);
    CHECK_FALSE(same.IsDown("Fire"));
    carry.Deactivate();
    CHECK_FALSE(carry.IsActive());
    CHECK_FALSE(carry.IsDown("Fire"));
    // Closed, it goes on taking Fire until the frame ends, so the press that closed it reaches nothing below.
    CHECK_FALSE(weapon.IsDown("Fire"));
    world.Step();
    world.Poll();
    // Contexts of the same priority both see it.
    CHECK(weapon.IsDown("Fire"));
    CHECK(same.IsDown("Fire"));

    // Reopened within the frame, it goes on as if it had never closed.
    carry.Activate(&player);
    carry.Deactivate();
    carry.Activate(&player);
    world.Step();
    world.Poll();
    CHECK(carry.IsDown("Fire"));
    CHECK_FALSE(weapon.IsDown("Fire"));
    carry.Deactivate();
    world.Step();

    // A consumed press reaches no one else that frame; holding goes on.
    world.Release(Spot::Key::F);
    world.Step();
    world.Press(Spot::Key::F);
    world.Poll();
    CHECK(weapon.IsPressed("Fire"));
    weapon.Consume("Fire");
    CHECK_FALSE(same.IsPressed("Fire"));
    CHECK(same.IsDown("Fire"));
    world.Step();

    // A context gone, or its controller, takes nothing more.
    {
        Gameplay::InputContext gone{{.name = "Gone", .priority = 50, .actions = {"Use"}}};
        gone.Activate(&player);
        CHECK_FALSE(weapon.IsAvailable("Use"));
    }
    CHECK(weapon.IsAvailable("Use"));
    player.GetEntity().Destroy();
    world.Step();
    CHECK_FALSE(weapon.IsActive());
    CHECK_FALSE(weapon.IsAvailable("Use"));
}

TEST_CASE("An interactor uses what its pawn looks at", "[sandbox][gameplay]") {
    CoreWorld world;
    auto& pawn = world.AddPawn("Pawn");
    world.AddPlayer(&pawn.GetEntity());
    auto& interactor = pawn.GetEntity().AddComponent<Gameplay::Interactor>();
    auto& grabber = pawn.GetEntity().AddComponent<Grabber>();
    // What is hit is the door's panel; the interactable is found above it.
    auto& doorway = world.scene.CreateEntity("Door");
    doorway.GetTransform().SetPosition({0.0f, 1.0f, -2.0f});
    auto& panel = doorway.CreateChild("Panel");
    panel.AddComponent<Spot::BoxCollider>(Spot::Vector3{1.0f, 1.0f, 1.0f});
    auto& door = doorway.AddComponent<Door>();
    door.prompt = "Open";
    world.Step();
    world.Step();

    CHECK(interactor.GetPawn() == &pawn);
    CHECK(interactor.GetFocus() == panel.GetId());
    CHECK(interactor.GetPrompt() == "Open");
    world.Press(Spot::Key::E);
    world.Step();
    CHECK(door.opened == 1);
    world.Release(Spot::Key::E);
    world.Step();
    CHECK(door.opened == 1);

    // Blocked by the pawn's state.
    pawn.SetState("Dead", true);
    world.Step();
    CHECK_FALSE(interactor.HasFocus());
    CHECK(interactor.GetPrompt().empty());
    world.Press(Spot::Key::E);
    world.Step();
    world.Release(Spot::Key::E);
    CHECK(door.opened == 1);
    pawn.SetState("Dead", false);

    // A context above that takes Use leaves nothing in focus.
    Gameplay::InputContext carrying{{.name = "Carry", .priority = 30, .actions = {"Use"}}};
    carrying.Activate(pawn.GetController());
    world.Step();
    CHECK_FALSE(interactor.HasFocus());
    carrying.Deactivate();
    world.Step();
    world.Step();
    CHECK(interactor.HasFocus());

    // Without an interactable, a handler on the interactor's entity uses what was hit.
    doorway.Destroy();
    auto& crate = AddBox(world.scene, {0.0f, 1.0f, -2.5f});
    crate.AddComponent<Spot::Rigidbody>().SetKinematic(true);
    world.Step();
    CHECK(interactor.GetFocus() == crate.GetId());
    CHECK(interactor.GetPrompt() == "Grab");
    CHECK(interactor.Interact());
    CHECK(grabber.grabbed == crate.GetId());

    // Out of reach, nothing.
    interactor.reach = 1.5f;
    world.Step();
    CHECK_FALSE(interactor.HasFocus());
    CHECK_FALSE(interactor.Interact());
}

TEST_CASE("The press an interactor uses reaches no other context", "[sandbox][gameplay]") {
    CoreWorld world;
    auto& pawn = world.AddPawn("Pawn");
    world.AddPlayer(&pawn.GetEntity());
    pawn.GetEntity().AddComponent<Gameplay::Interactor>();
    auto& reader = pawn.GetEntity().AddComponent<UseReader>();
    auto& box = AddBox(world.scene, {0.0f, 1.0f, -2.0f});
    auto& door = box.AddComponent<Door>();
    world.Step();
    world.Step();

    // The door takes the press, so the part reading Use after the interactor does not see it.
    world.Press(Spot::Key::E);
    world.Step();
    world.Release(Spot::Key::E);
    world.Step();
    CHECK(door.opened == 1);
    CHECK(reader.presses == 0);

    // With nothing to use, the press goes on to it.
    box.Destroy();
    world.Step();
    world.Press(Spot::Key::E);
    world.Step();
    CHECK(reader.presses == 1);
}

TEST_CASE("User settings reach held items and the occupant without visiting another pawn", "[sandbox][gameplay]") {
    CoreWorld world;
    auto& pawn = world.AddPawn("Walker");
    auto& held = pawn.GetEntity().CreateChild("Item").AddComponent<SettingsPart>();
    auto& nested = pawn.GetEntity().CreateChild("Separate pawn");
    nested.AddComponent<Gameplay::Pawn>();
    auto& separate = nested.AddComponent<SettingsPart>();
    auto& vehicle = world.AddPawn("Vehicle");
    auto& driven = vehicle.GetEntity().AddComponent<SettingsPart>();
    auto& controller = world.AddPlayer(&pawn.GetEntity());
    world.Step();
    vehicle.SetOccupant(&pawn);
    controller.Possess(vehicle);
    Gameplay::UserSettings settings;
    settings.sensitivity = 0.004f;
    settings.Apply(world.scene);
    CHECK(held.applied == 1);
    CHECK(held.sensitivity == settings.sensitivity);
    CHECK(driven.applied == 1);
    CHECK(separate.applied == 0);
}

namespace {
class EarlyInputProbe final : public Spot::Component, public Gameplay::PlayerInputListener {
public:
    explicit EarlyInputProbe(bool fail = false) : m_fail(fail) {}
    int calls = 0;
    bool IsInputBeforeFixedSteps() const override { return true; }
    void OnPlayerInput(Gameplay::PlayerController&) override {
        ++calls;
        if (m_fail) {
            throw std::runtime_error("Input adapter failed");
        }
    }

private:
    bool m_fail;
};
} // namespace

TEST_CASE("A failing early input adapter faults only its controller", "[sandbox][gameplay]") {
    CoreWorld world;
    auto& brokenPawn = world.AddPawn("Broken");
    brokenPawn.GetEntity().AddComponent<EarlyInputProbe>(true);
    auto& broken = world.AddPlayer(&brokenPawn.GetEntity());
    auto& healthyPawn = world.AddPawn("Healthy");
    auto& probe = healthyPawn.GetEntity().AddComponent<EarlyInputProbe>();
    auto& healthy = world.AddPlayer(&healthyPawn.GetEntity());
    const auto errors = std::make_shared<Spot::Tests::CaptureSink>();
    {
        const Spot::Tests::ScopedSinks sinks{{errors}};
        world.Step();
        world.Step();
    }
    CHECK(broken.IsFaulted());
    CHECK_FALSE(healthy.IsFaulted());
    CHECK(probe.calls == 2);
    CHECK(std::ranges::any_of(errors->GetRecords(), [](const auto& record) {
        return record.message.find("threw in OnPlayerInput") != std::string::npos;
    }));
}
