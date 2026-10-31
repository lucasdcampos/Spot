#include "playground_fixture.h"

#include "gameplay/camera/first_person_view.h"
#include "gameplay/character/carrier.h"
#include "gameplay/character/character_movement.h"
#include "gameplay/character/flashlight.h"
#include "gameplay/core/body_authority.h"
#include "gameplay/core/interactor.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/core/players.h"
#include "gameplay/game/game_mode.h"
#include "gameplay/health/health.h"
#include "gameplay/network/network_players.h"
#include "gameplay/network/remote_controller.h"
#include "gameplay/vehicles/helicopter.h"
#include "gameplay/vehicles/vehicle_seat.h"
#include "gameplay/vehicles/vehicle_wheel.h"
#include "gameplay/vehicles/wheeled_vehicle.h"
#include "npc.h"
#include "physics_gun.h"
#include "tool_gun.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>

namespace {

const Spot::AssetId PlaygroundId{0x51, 0x52};

// One machine of a room: the playground as the game loads it, with its own input, bound to a session.
struct Machine {
    Spot::InputState input;
    Spot::InputActionMap actions;
    TempDirectory userData;
    Spot::Scene scene;
    std::shared_ptr<Spot::NetworkSession> session;
    Spot::NetworkSystem* system = nullptr;

    // A dedicated server's machine has no player of its own.
    void Load(const Spot::GameSettings& game, Spot::AssetManager& assets, std::unique_ptr<Spot::NetHost> host,
              bool dedicated = false) {
        game.sections.input.Apply(actions);
        actions.SetDefaults();
        const auto snapshot = Spot::Process::GetEnvironmentValue("SPOT_SANDBOX_TEST_ASSETS");
        const auto sources = snapshot && !snapshot->empty() ? std::filesystem::path(*snapshot)
                                                            : std::filesystem::path(SANDBOX_ASSETS_DIR);
        REQUIRE(scene.LoadFromFile(sources / "scenes/playground.scene", &assets));
        scene.SetSourceAsset(PlaygroundId);
        if (dedicated) {
            Gameplay::Players::RemoveAll(scene);
        }
        session = std::make_shared<Spot::NetworkSession>(std::move(host));
        // Shown as soon as it arrives, as the steps here take no time.
        system = &scene.AddSystem<Spot::NetworkSystem>(
            session, Spot::NetworkSystemConfig{.interpolationDelay = 0.0f, .maxInterpolationDelay = 0.0f});
        Step();
        if (auto* settings = scene.FindComponent<Gameplay::SettingsMenu>()) {
            settings->SetSettingsDirectory(userData.GetPath());
        }
    }

    void Step() {
        Spot::Input::SetState(&input);
        Spot::Input::SetActionMap(&actions);
        actions.Update(input);
        scene.Update(Spot::Scene::DefaultFixedTimeStep);
        input.BeginFrame();
    }

    void Press(Spot::Key key) { input.OnEvent(Spot::KeyPressedEvent{key, false}); }
    void Release(Spot::Key key) { input.OnEvent(Spot::KeyReleasedEvent{key}); }

    Gameplay::Pawn* GetPawn() { return Gameplay::Players::GetPawn(scene); }
    // This machine's copy of the entity another machine has.
    Spot::Entity* Find(const Spot::Entity& other) {
        const Spot::NetworkObject* object = Spot::Network::GetObject(other);
        return object ? system->FindEntity(object->GetNetworkId()) : nullptr;
    }
};

// A host and a guest in the playground, over the loopback network, in one process; or a dedicated server, which nobody
// plays on, and two guests.
struct Room {
    std::shared_ptr<Spot::Tests::CaptureSink> log = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks{{log}};
    Spot::Tests::RegisteredAssetManager assets{SANDBOX_IMPORTED_DIR};
    Spot::InputState* previousInput = Spot::Input::GetState();
    Spot::InputActionMap* previousActions = Spot::Input::GetActionMap();
    Spot::AssetManager* previousAssets = Spot::Assets::GetManager();
    float previousScale = Spot::Time::GetTimeScale();
    Spot::LoopbackNetwork network;
    Machine host;
    Machine guest;
    Machine other;
    bool dedicated = false;

    explicit Room(bool dedicatedServer = false) : dedicated(dedicatedServer) {
        Spot::Detail::LoadGameCode();
        Spot::Time::SetTimeScale(1);
        Spot::Detail::SetAssetManager(&assets);
        const auto game = Spot::GameSettings::LoadFromFile(std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() /
                                                           Spot::GameSettings::FileName);
        REQUIRE(game);
        auto listen = MakeHost();
        REQUIRE(listen->Listen(27015, 4));
        host.Load(*game, assets, std::move(listen), dedicated);
        auto join = MakeHost();
        REQUIRE(join->Connect({"localhost", 27015}) != Spot::NetPeerId::Invalid);
        guest.Load(*game, assets, std::move(join));
        if (dedicated) {
            auto second = MakeHost();
            REQUIRE(second->Connect({"localhost", 27015}) != Spot::NetPeerId::Invalid);
            other.Load(*game, assets, std::move(second));
        }
        Run(120);
    }

    ~Room() {
        other.scene.Clear();
        guest.scene.Clear();
        host.scene.Clear();
        Spot::Input::SetState(previousInput);
        Spot::Input::SetActionMap(previousActions);
        Spot::Detail::SetAssetManager(previousAssets);
        Spot::Time::SetTimeScale(previousScale);
        for (const auto& record : log->GetRecords()) {
            INFO(record.message);
            CHECK(record.message.find(" threw in ") == std::string::npos);
        }
    }

    std::unique_ptr<Spot::NetHost> MakeHost() {
        return std::make_unique<Spot::NetHost>(network.CreateTransport(),
                                               Spot::NetHostConfig{.channels = Spot::NetworkSession::ChannelCount});
    }

    void Run(int frames) {
        for (int i = 0; i < frames; ++i) {
            host.Step();
            guest.Step();
            if (dedicated) {
                other.Step();
            }
        }
    }

    // The host's copy of the guest's pawn.
    Spot::Entity* GetGuestOnHost() {
        Gameplay::Pawn* pawn = guest.GetPawn();
        return pawn ? host.Find(pawn->GetEntity()) : nullptr;
    }

    Spot::Entity& Spawn(const std::string& prefab, Spot::Vector3 position) {
        const auto asset = assets.Load<Spot::Prefab>(prefab);
        REQUIRE(asset);
        Spot::Entity* entity = host.scene.Instantiate(asset);
        REQUIRE(entity);
        entity->GetTransform().SetWorldPosition(position);
        return *entity;
    }

    // Turns the guest's view toward the point.
    void LookAt(Spot::Vector3 point) {
        auto* view = guest.GetPawn()->GetEntity().GetComponent<Gameplay::FirstPersonView>();
        const Spot::Vector3 direction = Spot::Normalize(point - view->GetEyePosition());
        view->SetLook(std::atan2(-direction.x, -direction.z), std::asin(std::clamp(direction.y, -1.0f, 1.0f)));
    }
};

// Where the guest's pawn is, a little ahead along where it faces, on the ground.
Spot::Vector3 InFront(Gameplay::Pawn& pawn, float distance) {
    const Gameplay::ViewPoint view = pawn.GetViewPoint();
    const Spot::Vector3 flat = Spot::Normalize(Spot::Vector3{view.direction.x, 0.0f, view.direction.z});
    return pawn.GetEntity().GetTransform().GetWorldPosition() + flat * distance;
}

} // namespace

TEST_CASE("A guest in the playground moves, switches, carries, shoots and rides as the host sees it",
          "[sandbox][network][playground]") {
    Room room;
    Gameplay::Pawn* pawn = room.guest.GetPawn();
    REQUIRE(pawn);
    Spot::Entity* onHost = room.GetGuestOnHost();
    REQUIRE(onHost);
    const auto* players = room.guest.scene.FindComponent<Gameplay::NetworkPlayers>();
    REQUIRE(players);
    const std::uint32_t corrections = players->GetCorrections();

    // Running, aiming and jumping on the guest's own machine, as the host sees it, without being set right.
    room.guest.Press(Spot::Key::W);
    room.guest.Press(Spot::Key::LeftShift);
    room.Run(40);
    room.guest.input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Right});
    room.Run(20);
    room.guest.input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Right});
    room.guest.Press(Spot::Key::Space);
    room.Run(2);
    room.guest.Release(Spot::Key::Space);
    room.Run(40);
    room.guest.Release(Spot::Key::W);
    room.guest.Release(Spot::Key::LeftShift);
    room.Run(40);
    CHECK(Spot::Distance(pawn->GetEntity().GetTransform().GetWorldPosition(),
                         onHost->GetTransform().GetWorldPosition()) < 0.05f);
    CHECK(players->GetCorrections() <= corrections + 1);

    // Switching items shows on the other machine.
    auto& guestItems = *pawn->GetEntity().GetComponent<Gameplay::Inventory>();
    room.guest.Press(Spot::Key::Digit2);
    room.Run(2);
    room.guest.Release(Spot::Key::Digit2);
    room.Run(20);
    CHECK(guestItems.GetSelected() == 1);
    CHECK(onHost->GetComponent<Gameplay::Inventory>()->GetSelected() == guestItems.GetSelected());
    Gameplay::Pawn* hostPawn = room.host.GetPawn();
    REQUIRE(hostPawn);
    hostPawn->GetEntity().GetComponent<Gameplay::Inventory>()->Select(2);
    room.Run(20);
    Spot::Entity* hostOnGuest = room.guest.Find(hostPawn->GetEntity());
    REQUIRE(hostOnGuest);
    CHECK(hostOnGuest->GetComponent<Gameplay::Inventory>()->GetSelected() == 2);

    // A crate in front of the guest can be picked up from there, and dropped.
    Spot::Entity& crate =
        room.Spawn("prefabs/spawnables/props/crate.prefab", InFront(*pawn, 1.8f) + Spot::Vector3{0, 0.6f, 0});
    room.Run(60);
    Spot::Entity* crateOnGuest = room.guest.Find(crate);
    REQUIRE(crateOnGuest);
    room.LookAt(crateOnGuest->GetTransform().GetWorldPosition());
    room.Run(4);
    const auto* interactor = pawn->GetEntity().GetComponent<Gameplay::Interactor>();
    CHECK(interactor->GetPrompt() == "Pick up");
    room.guest.Press(Spot::Key::E);
    room.Run(2);
    room.guest.Release(Spot::Key::E);
    room.Run(20);
    const auto* hostCarrier = onHost->GetComponent<Gameplay::Carrier>();
    CHECK(hostCarrier->GetHeld() == crate.GetId());
    CHECK(pawn->GetEntity().GetComponent<Gameplay::Carrier>()->GetHeld() == crateOnGuest->GetId());
    // Carried on the guest's machine, at once, rather than as the host last heard.
    const Spot::NetPeerId guestPeer = onHost->GetComponent<Spot::NetworkObject>()->GetOwner();
    CHECK(Gameplay::BodyAuthority::GetHolder(crate) == guestPeer);
    CHECK_FALSE(crateOnGuest->GetComponent<Spot::Rigidbody>()->IsKinematic());
    room.guest.Press(Spot::Key::E);
    room.Run(2);
    room.guest.Release(Spot::Key::E);
    room.Run(20);
    CHECK_FALSE(hostCarrier->IsHolding());

    // The guest's flashlight shows on the host.
    room.guest.Press(Spot::Key::F);
    room.Run(2);
    room.guest.Release(Spot::Key::F);
    room.Run(20);
    CHECK(pawn->GetEntity().GetComponent<Gameplay::Flashlight>()->IsOn());
    CHECK(onHost->GetComponent<Gameplay::Flashlight>()->IsOn());

    // The guest's physics gun grabs the crate on the host, and every machine knows what it holds.
    guestItems.Select(0);
    room.Run(20);
    auto* guestGun = SandboxTests::GetItem<PhysicsGun>(guestItems);
    auto* hostGun = SandboxTests::GetItem<PhysicsGun>(*onHost->GetComponent<Gameplay::Inventory>());
    REQUIRE(guestGun);
    REQUIRE(hostGun);
    room.LookAt(crateOnGuest->GetTransform().GetWorldPosition());
    room.Run(4);
    room.guest.input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left});
    room.Run(30);
    CHECK(hostGun->GetGrabbed() == crate.GetId());
    CHECK(guestGun->GetGrabbed() == crateOnGuest->GetId());
    room.guest.input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left});
    room.Run(30);
    CHECK_FALSE(hostGun->IsGrabbing());
    CHECK_FALSE(guestGun->IsGrabbing());

    // The guest's tool gun paints the crate, which the guest is then shown painted.
    guestItems.Select(guestItems.GetItemCount() - 1);
    room.Run(20);
    auto* hostTool = SandboxTests::GetItem<ToolGun>(*onHost->GetComponent<Gameplay::Inventory>());
    REQUIRE(hostTool);
    while (hostTool->mode != ToolGun::Mode::Color) {
        room.guest.Press(Spot::Key::R);
        room.Run(2);
        room.guest.Release(Spot::Key::R);
        room.Run(6);
    }
    room.Run(10);
    CHECK(SandboxTests::GetItem<ToolGun>(guestItems)->mode == ToolGun::Mode::Color);
    room.LookAt(crateOnGuest->GetTransform().GetWorldPosition());
    room.Run(4);
    room.guest.input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left});
    room.Run(2);
    room.guest.input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left});
    room.Run(30);
    const auto* hostRenderer = crate.GetComponent<Spot::MeshRenderer>();
    const auto* guestRenderer = crateOnGuest->GetComponent<Spot::MeshRenderer>();
    REQUIRE(hostRenderer);
    REQUIRE(guestRenderer);
    const Spot::Color painted = ToolGun::Palette[static_cast<std::size_t>(hostTool->color)].color;
    CHECK(hostRenderer->GetMaterial()->baseColor.r == painted.r);
    CHECK(guestRenderer->GetMaterial()->baseColor.r == painted.r);

    // A lamp its tool gun puts on the crate is on the crate on every machine, and the guest's undo takes it away.
    while (hostTool->mode != ToolGun::Mode::Lamp) {
        room.guest.Press(Spot::Key::R);
        room.Run(2);
        room.guest.Release(Spot::Key::R);
        room.Run(6);
    }
    room.LookAt(crateOnGuest->GetTransform().GetWorldPosition());
    room.Run(4);
    room.guest.input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left});
    room.Run(2);
    room.guest.input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left});
    room.Run(30);
    const auto lampsOn = [](const Spot::Entity& prop) {
        int lamps = 0;
        for (const Spot::Entity* child : prop.GetChildren()) {
            lamps += child->GetComponent<Spot::Light>() != nullptr ? 1 : 0;
        }
        return lamps;
    };
    CHECK(lampsOn(crate) == 1);
    CHECK(lampsOn(*crateOnGuest) == 1);
    room.guest.Press(Spot::Key::Z);
    room.Run(2);
    room.guest.Release(Spot::Key::Z);
    room.Run(30);
    CHECK(lampsOn(crate) == 0);
    CHECK(lampsOn(*crateOnGuest) == 0);

    // A rope it ties between two crates is tied on the guest's machine too.
    Spot::Entity& other = room.Spawn("prefabs/spawnables/props/crate.prefab",
                                     crate.GetTransform().GetWorldPosition() + Spot::Vector3{1.5f, 0.0f, 0.0f});
    room.Run(60);
    Spot::Entity* otherOnGuest = room.guest.Find(other);
    REQUIRE(otherOnGuest);
    while (hostTool->mode != ToolGun::Mode::Rope) {
        room.guest.Press(Spot::Key::R);
        room.Run(2);
        room.guest.Release(Spot::Key::R);
        room.Run(6);
    }
    for (Spot::Entity* target : {crateOnGuest, otherOnGuest}) {
        room.LookAt(target->GetTransform().GetWorldPosition());
        room.Run(4);
        room.guest.input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left});
        room.Run(2);
        room.guest.input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left});
        room.Run(10);
    }
    room.Run(30);
    const auto* rope = crate.GetComponent<Spot::DistanceJoint>();
    REQUIRE(rope);
    CHECK(rope->GetConnectedEntity() == other.GetId());
    const auto* ropeOnGuest = crateOnGuest->GetComponent<Spot::DistanceJoint>();
    REQUIRE(ropeOnGuest);
    CHECK(ropeOnGuest->GetConnectedEntity() == otherOnGuest->GetId());

    // Shots the guest sees hit an NPC hurt it on the host.
    Npc* npc = room.host.scene.FindComponent<Npc>();
    REQUIRE(npc);
    npc->GetEntity().GetTransform().SetWorldPosition(InFront(*pawn, 5.0f));
    room.Run(30);
    Spot::Entity* npcOnGuest = room.guest.Find(npc->GetEntity());
    REQUIRE(npcOnGuest);
    guestItems.Select(1);
    room.Run(30);
    REQUIRE(SandboxTests::GetFirearm(guestItems));
    const auto* health = npc->GetEntity().GetComponent<Gameplay::Health>();
    REQUIRE(health);
    const float before = health->GetHealth();
    for (int shot = 0; shot < 3; ++shot) {
        room.LookAt(npcOnGuest->GetTransform().GetWorldPosition() + Spot::Vector3{0.0f, 0.3f, 0.0f});
        room.guest.input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left});
        room.Run(2);
        room.guest.input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left});
        room.Run(20);
    }
    CHECK(health->GetHealth() < before);

    // A helicopter the host flies spins its rotor on the guest's machine.
    Spot::Entity& helicopter = room.Spawn("prefabs/spawnables/props/helicopter.prefab", InFront(*hostPawn, 8.0f));
    room.Run(30);
    REQUIRE(helicopter.GetComponentInChildren<Gameplay::VehicleSeat>()->Enter(*hostPawn));
    room.Run(120);
    Spot::Entity* helicopterOnGuest = room.guest.Find(helicopter);
    REQUIRE(helicopterOnGuest);
    CHECK(helicopterOnGuest->GetComponent<Gameplay::Helicopter>()->GetRotorSpeed() > 0.5f);

    // The guest gets into a car behind it, drives it at once on its own machine, pushing what it meets, and the host
    // sees it go.
    Spot::Entity& spawned =
        room.Spawn("prefabs/spawnables/props/car.prefab", InFront(*pawn, -3.0f) + Spot::Vector3{0, 0.5f, 0});
    room.Run(90);
    Spot::Entity* spawnedOnGuest = room.guest.Find(spawned);
    REQUIRE(spawnedOnGuest);
    room.LookAt(spawnedOnGuest->GetTransform().GetWorldPosition());
    room.Run(4);
    room.guest.Press(Spot::Key::E);
    room.Run(2);
    room.guest.Release(Spot::Key::E);
    room.Run(30);
    // That one, or one of the playground's parked in the way.
    Gameplay::Pawn* driven = room.guest.GetPawn();
    REQUIRE(driven);
    REQUIRE(driven->GetEntity().GetComponent<Gameplay::WheeledVehicle>());
    Spot::Entity* carOnGuest = &driven->GetEntity();
    Spot::Entity* hostCar = room.host.Find(*carOnGuest);
    REQUIRE(hostCar);
    Spot::Entity& car = *hostCar;
    CHECK(carOnGuest->GetComponent<Spot::NetworkObject>()->HasAuthority());
    const Spot::Vector3 parked = car.GetTransform().GetWorldPosition();
    room.guest.Press(Spot::Key::W);
    room.Run(90);
    room.guest.Release(Spot::Key::W);
    room.Run(10);
    CHECK(Spot::Distance(car.GetTransform().GetWorldPosition(), parked) > 2.0f);
    CHECK(Spot::Distance(car.GetTransform().GetWorldPosition(), carOnGuest->GetTransform().GetWorldPosition()) < 0.5f);
    bool spinning = false;
    for (const auto* wheel : car.GetComponent<Gameplay::WheeledVehicle>()->GetWheels()) {
        spinning = spinning || std::abs(wheel->GetSpin()) > 0.1f;
    }
    CHECK(spinning);

    // Killed, the guest sees how long until it comes back, and comes back.
    auto* guestHealth = onHost->GetComponent<Gameplay::Health>();
    guestHealth->TakeDamage({.amount = 1000.0f});
    room.Run(10);
    CHECK(pawn->GetEntity().GetComponent<Gameplay::Health>()->IsDead());
    auto* mode = Gameplay::GameMode::Get(room.guest.scene);
    REQUIRE(mode);
    CHECK(mode->GetRespawnTimeLeft(*Gameplay::Players::Get(room.guest.scene)) > 0.5f);
    room.Run(static_cast<int>((mode->respawnTime + 1.0f) * 60.0f));
    REQUIRE(room.guest.GetPawn());
    CHECK_FALSE(room.guest.GetPawn()->GetEntity().GetComponent<Gameplay::Health>()->IsDead());
}

TEST_CASE("A dedicated server plays nobody and keeps two guests in the playground in sync",
          "[sandbox][network][playground]") {
    Room room(true);
    CHECK_FALSE(Gameplay::Players::Get(room.host.scene));
    int remote = 0;
    room.host.scene.ForEachComponent<Gameplay::RemoteController>(
        [&](Gameplay::RemoteController& controller) { remote += controller.GetPawn() ? 1 : 0; });
    CHECK(remote == 2);
    Gameplay::Pawn* first = room.guest.GetPawn();
    Gameplay::Pawn* second = room.other.GetPawn();
    REQUIRE(first);
    REQUIRE(second);
    // Each sees the other, and only the two of them play.
    Spot::Entity* firstOnSecond = room.other.Find(first->GetEntity());
    REQUIRE(firstOnSecond);
    CHECK(room.guest.Find(second->GetEntity()));
    int characters = 0;
    room.other.scene.ForEachComponent<Gameplay::CharacterMovement>([&](Gameplay::CharacterMovement&) { ++characters; });
    CHECK(characters == 2);

    // One walks; the server and the other guest follow.
    const Spot::Vector3 start = first->GetEntity().GetTransform().GetWorldPosition();
    room.guest.Press(Spot::Key::W);
    room.Run(60);
    room.guest.Release(Spot::Key::W);
    room.Run(40);
    const Spot::Vector3 walked = first->GetEntity().GetTransform().GetWorldPosition();
    CHECK(Spot::Distance(walked, start) > 1.0f);
    Spot::Entity* firstOnHost = room.host.Find(first->GetEntity());
    REQUIRE(firstOnHost);
    CHECK(Spot::Distance(firstOnHost->GetTransform().GetWorldPosition(), walked) < 0.05f);
    CHECK(Spot::Distance(firstOnSecond->GetTransform().GetWorldPosition(), walked) < 0.05f);
}
