#include "gameplay_world.h"

#include "gameplay/core/interactor.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/pawn_request.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/core/player_input.h"
#include "gameplay/core/players.h"
#include "gameplay/effects/effect_library.h"
#include "gameplay/effects/flash.h"
#include "gameplay/game/game_mode.h"
#include "gameplay/game/spawn_point.h"
#include "gameplay/network/network_players.h"
#include "gameplay/network/pawn_intents.h"
#include "gameplay/network/player_info.h"
#include "gameplay/network/remote_controller.h"
#include "gameplay/network/session.h"
#include "gameplay/vehicles/vehicle.h"
#include "gameplay/vehicles/vehicle_seat.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace {

// A part of a pawn that walks where its intent says, only where the pawn's state is decided.
class Walker final : public Spot::Component, public Gameplay::PlayerInputListener {
public:
    Spot::Vector2 direction;
    int jumps = 0;
    bool jump = false;

    void OnPlayerInput(Gameplay::PlayerController& /*player*/) override {}
    void SerializeIntent(Spot::Archive& archive) override {
        Spot::Vector2 wanted = direction;
        bool pressed = jump;
        archive.Field("direction", wanted);
        archive.Field("jump", pressed);
        if (archive.IsLoading()) {
            direction = wanted;
            // A press counts once, however many frames it lasts.
            jumps += pressed && !jump ? 1 : 0;
            jump = pressed;
        }
    }

protected:
    void OnFixedUpdate(float deltaTime) override {
        if (Spot::Network::HasAuthority(GetEntity())) {
            GetEntity().GetTransform().Translate(
                {direction.x * 4.0f * deltaTime, 0.0f, -direction.y * 4.0f * deltaTime});
        }
    }
};

// A predicted part that walks where its intent says: on its player's machine at once, and on the server step by step
// as that machine stepped.
class Stepper final : public Spot::Component,
                      public Gameplay::PlayerInputListener,
                      public Gameplay::PredictedPart {
public:
    Spot::Vector2 direction;
    // Grows with each step walking, as a state the server sends back.
    float stride = 0.0f;

    void OnPlayerInput(Gameplay::PlayerController& /*player*/) override {}
    void SerializeIntent(Spot::Archive& archive) override { archive.Field("direction", direction); }
    bool IsIntentPerStep() const override { return true; }
    void Step(float deltaTime, bool /*replaying*/) override {
        stride += Spot::Length(direction) * deltaTime;
        GetEntity().GetTransform().Translate({direction.x * 4.0f * deltaTime, 0.0f, -direction.y * 4.0f * deltaTime});
    }
    void SerializeStepState(Spot::Archive& archive) override { archive.Field("stride", stride); }

protected:
    void OnFixedUpdate(float deltaTime) override {
        const Spot::NetworkObject* object = Spot::Network::GetObject(GetEntity());
        if (object && object->IsPredicted() ? Spot::Network::IsClient(GetScene())
                                            : Spot::Network::HasAuthority(GetEntity())) {
            Step(deltaTime, false);
        }
    }
};

const Spot::AssetId PlayerPrefab{11, 22};
const Spot::AssetId VehiclePrefab{33, 44};
const Spot::AssetId InfoPrefab{55, 66};

class TestVehicle final : public Gameplay::Vehicle {
public:
    float GetForwardSpeed() const override { return GetInput().drive * 6.0f; }

protected:
    void OnFixedUpdate(float deltaTime) override {
        if (Spot::Network::HasAuthority(GetEntity())) {
            GetEntity().GetTransform().Translate({0.0f, 0.0f, -GetForwardSpeed() * deltaTime});
        }
    }
};

void AddCamera(Gameplay::Pawn& pawn) {
    auto& camera = pawn.GetEntity().CreateChild("Camera");
    camera.AddComponent<Spot::Camera>().SetEnabled(false);
    pawn.viewCamera = camera.GetId();
}

Spot::AssetRef<Spot::Prefab> MakePlayerPrefab(bool predicted = false) {
    // Before any world does, as the prefab saves the game's components.
    Spot::Detail::LoadGameCode();
    Spot::RegisterComponent<Walker>("Tests.Walker");
    Spot::RegisterComponent<Stepper>("Tests.Stepper");
    Spot::Scene authoring;
    Spot::Entity& player = authoring.CreateEntity("Player");
    player.AddComponent<Spot::NetworkObject>().predicted = predicted;
    if (predicted) {
        player.AddComponent<Stepper>();
    }
    player.AddComponent<Spot::NetworkTransform>().maxExtrapolation = 0.0f;
    AddCamera(player.AddComponent<Gameplay::Pawn>());
    player.AddComponent<Walker>();
    player.AddComponent<Gameplay::Interactor>();
    auto prefab = Spot::Prefab::FromString(player.SaveToString());
    REQUIRE(prefab);
    return Spot::AssetRef<Spot::Prefab>(PlayerPrefab, std::make_shared<const Spot::Prefab>(std::move(*prefab)));
}

Spot::AssetRef<Spot::Prefab> MakeVehiclePrefab() {
    Spot::RegisterComponent<TestVehicle>("Tests.NetworkVehicle");
    Spot::Scene authoring;
    Spot::Entity& entity = authoring.CreateEntity("Vehicle");
    entity.AddComponent<Spot::NetworkObject>();
    entity.AddComponent<Spot::NetworkTransform>().maxExtrapolation = 0.0f;
    AddCamera(entity.AddComponent<TestVehicle>());
    entity.AddComponent<Spot::BoxCollider>(Spot::Vector3{1.0f, 1.0f, 1.0f});
    entity.AddComponent<Gameplay::VehicleSeat>().poseState = "Driving";
    auto prefab = Spot::Prefab::FromString(entity.SaveToString());
    REQUIRE(prefab);
    return Spot::AssetRef<Spot::Prefab>(VehiclePrefab, std::make_shared<const Spot::Prefab>(std::move(*prefab)));
}

Spot::AssetRef<Spot::Prefab> MakeInfoPrefab() {
    Spot::Scene authoring;
    auto& root = authoring.CreateEntity("Player Info");
    root.AddComponent<Spot::NetworkObject>();
    root.AddComponent<Gameplay::PlayerInfo>();
    auto prefab = Spot::Prefab::FromString(root.SaveToString());
    REQUIRE(prefab);
    return {InfoPrefab, std::make_shared<const Spot::Prefab>(std::move(*prefab))};
}

struct Machine : GameplayTests::World {
    std::shared_ptr<Spot::NetworkSession> session;
    Spot::NetworkSystem* system = nullptr;

    void Start(std::unique_ptr<Spot::NetHost> host, const Spot::AssetRef<Spot::Prefab>& prefab,
               const Spot::AssetRef<Spot::Prefab>& vehicle = {}, const Spot::AssetRef<Spot::Prefab>& info = {}) {
        session = std::make_shared<Spot::NetworkSession>(std::move(host));
        scene.CreateEntity("Players").AddComponent<Gameplay::NetworkPlayers>().playerInfoPrefab = info;
        Spot::NetworkSystemConfig config{.interpolationDelay = 0.0f, .maxInterpolationDelay = 0.0f};
        config.loadPrefab = [prefab, vehicle, info](Spot::AssetId id) {
            return id == PlayerPrefab    ? prefab
                   : id == VehiclePrefab ? vehicle
                   : id == InfoPrefab    ? info
                                         : Spot::AssetRef<Spot::Prefab>();
        };
        system = &scene.AddSystem<Spot::NetworkSystem>(session, config);
    }

    void Step() {
        Spot::Input::SetState(&input);
        Spot::Input::SetActionMap(&actions);
        GameplayTests::World::Step();
    }
};

std::unique_ptr<Spot::NetHost> MakeHost(Spot::LoopbackNetwork& network) {
    return std::make_unique<Spot::NetHost>(std::make_unique<Spot::ConditionedNetTransport>(network.CreateTransport()),
                                           Spot::NetHostConfig{.channels = Spot::NetworkSession::ChannelCount});
}

struct Session {
    bool predicted = false;
    Spot::LoopbackNetwork network;
    Spot::AssetRef<Spot::Prefab> prefab = MakePlayerPrefab(predicted);
    Spot::AssetRef<Spot::Prefab> vehiclePrefab = MakeVehiclePrefab();
    Spot::AssetRef<Spot::Prefab> infoPrefab = MakeInfoPrefab();
    Machine server;
    Machine client;

    explicit Session(bool predictedPlayer = false) : predicted(predictedPlayer) {
        // A dedicated server: its rules, and spawn points, but no player of its own.
        auto& mode = server.scene.CreateEntity("Rules").AddComponent<Gameplay::GameMode>();
        mode.playerPrefab = prefab;
        auto& spawn = server.scene.CreateEntity("Spawn");
        spawn.AddComponent<Gameplay::SpawnPoint>();
        spawn.GetTransform().SetPosition({0.0f, 1.0f, 0.0f});
        auto host = MakeHost(network);
        REQUIRE(host->Listen(9000, 4));
        server.Start(std::move(host), prefab, vehiclePrefab, infoPrefab);

        client.scene.CreateEntity("Controller").AddComponent<Gameplay::PlayerController>();
        auto join = MakeHost(network);
        REQUIRE(join->Connect({"localhost", 9000}) != Spot::NetPeerId::Invalid);
        client.Start(std::move(join), prefab, vehiclePrefab, infoPrefab);
    }

    Gameplay::VehicleSeat& AddVehicle() {
        auto* entity = server.scene.Instantiate(vehiclePrefab);
        REQUIRE(entity);
        entity->GetTransform().SetPosition({0.0f, 1.0f, -2.0f});
        return *entity->GetComponent<Gameplay::VehicleSeat>();
    }

    void Join(Machine& machine) {
        machine.scene.CreateEntity("Controller").AddComponent<Gameplay::PlayerController>();
        auto host = MakeHost(network);
        REQUIRE(host->Connect({"localhost", 9000}) != Spot::NetPeerId::Invalid);
        machine.Start(std::move(host), prefab, vehiclePrefab, infoPrefab);
    }

    void Step(int frames = 1) {
        for (int i = 0; i < frames; ++i) {
            server.Step();
            client.Step();
        }
    }
};

Gameplay::RemoteController* FindRemote(Spot::Scene& scene) {
    return scene.FindComponent<Gameplay::RemoteController>();
}

} // namespace

TEST_CASE("A client's player gets a pawn on the server, owns it and possesses its copy",
          "[sandbox][gameplay][network]") {
    Session session;
    session.Step(12);

    Gameplay::RemoteController* remote = FindRemote(session.server.scene);
    REQUIRE(remote);
    REQUIRE(remote->GetPawn());
    CHECK(remote->GetPeer() == session.server.system->GetReadyPeers().at(0));
    Spot::NetworkObject* serverObject = remote->GetPawn()->GetEntity().GetComponent<Spot::NetworkObject>();
    CHECK(serverObject->GetOwner() == remote->GetPeer());
    // The server decides where it is.
    CHECK(serverObject->HasAuthority());

    Gameplay::PlayerController* player = Gameplay::Players::Get(session.client.scene);
    REQUIRE(player);
    Gameplay::Pawn* pawn = player->GetPawn();
    REQUIRE(pawn);
    const Spot::NetworkObject* clientObject = pawn->GetEntity().GetComponent<Spot::NetworkObject>();
    CHECK(clientObject->IsOwnedLocally());
    CHECK_FALSE(clientObject->HasAuthority());
    CHECK(clientObject->GetNetworkId() == serverObject->GetNetworkId());
}

TEST_CASE("A client's intents move its pawn on the server, which every machine then shows",
          "[sandbox][gameplay][network]") {
    Session session;
    session.Step(12);
    Gameplay::Pawn* pawn = Gameplay::Players::GetPawn(session.client.scene);
    REQUIRE(pawn);
    Spot::Entity& serverBody = FindRemote(session.server.scene)->GetPawn()->GetEntity();

    auto& walker = *pawn->GetEntity().GetComponent<Walker>();
    walker.direction = {1.0f, 0.0f};
    walker.jump = true;
    session.Step();
    walker.jump = false;
    session.Step(30);
    walker.direction = {};
    session.Step(12);

    // The client's own copy did not move by itself: the server moved it, and the client shows where.
    const float moved = serverBody.GetTransform().GetWorldPosition().x;
    CHECK(moved > 1.0f);
    CHECK(pawn->GetEntity().GetTransform().GetWorldPosition().x == Catch::Approx(moved).margin(0.01f));
    // The press of one frame reached the server once.
    CHECK(serverBody.GetComponent<Walker>()->jumps == 1);
}

TEST_CASE("A predicted pawn moves on its player's machine at once and is corrected by the server",
          "[sandbox][gameplay][network]") {
    Session session(true);
    session.Step(12);
    Gameplay::Pawn* pawn = Gameplay::Players::GetPawn(session.client.scene);
    REQUIRE(pawn);
    REQUIRE(pawn->GetEntity().GetComponent<Spot::NetworkObject>()->IsPredicted());
    Spot::Entity& serverBody = FindRemote(session.server.scene)->GetPawn()->GetEntity();
    REQUIRE(serverBody.GetComponent<Spot::NetworkObject>()->IsPredicted());
    Spot::Entity& clientBody = pawn->GetEntity();
    const Spot::Vector3 start = clientBody.GetTransform().GetWorldPosition();
    CHECK(Spot::Distance(start, serverBody.GetTransform().GetWorldPosition()) < 0.01f);

    // The player's machine does not wait for the server.
    auto& stepper = *clientBody.GetComponent<Stepper>();
    stepper.direction = {1.0f, 0.0f};
    session.client.Step();
    CHECK(clientBody.GetTransform().GetWorldPosition().x > start.x + 0.05f);
    CHECK(serverBody.GetTransform().GetWorldPosition().x == Catch::Approx(start.x).margin(0.001f));

    session.Step(30);
    stepper.direction = {};
    session.Step(12);
    // The server took the same steps, and agrees.
    const Spot::Vector3 moved = serverBody.GetTransform().GetWorldPosition();
    CHECK(moved.x > start.x + 1.5f);
    CHECK(Spot::Distance(clientBody.GetTransform().GetWorldPosition(), moved) < 0.01f);
    CHECK(serverBody.GetComponent<Stepper>()->stride == Catch::Approx(stepper.stride).margin(0.001f));

    // What the server does to it, as pushing it aside, reaches the player's machine, which keeps walking from there.
    serverBody.GetTransform().Translate({0.0f, 0.0f, 3.0f});
    stepper.direction = {1.0f, 0.0f};
    session.Step(20);
    stepper.direction = {};
    session.Step(12);
    const Spot::Vector3 pushed = serverBody.GetTransform().GetWorldPosition();
    CHECK(pushed.z == Catch::Approx(moved.z + 3.0f).margin(0.01f));
    CHECK(pushed.x > moved.x + 1.0f);
    CHECK(Spot::Distance(clientBody.GetTransform().GetWorldPosition(), pushed) < 0.01f);
}

TEST_CASE("Effects a predicting player already showed are not sent back to that player",
          "[sandbox][gameplay][network]") {
    Session session(true);
    Machine observer;
    const auto addLibrary = [](Machine& machine) {
        Gameplay::EffectDefinition flash{.name = "flash"};
        flash.light = {.color = {1.0f, 1.0f, 1.0f, 1.0f}, .range = 4.0f, .intensity = 10.0f, .lifetime = 1.0f};
        machine.scene.CreateEntity("Effects").AddComponent<Gameplay::EffectLibrary>().effects.push_back(flash);
    };
    addLibrary(session.server);
    addLibrary(session.client);
    session.Join(observer);
    addLibrary(observer);
    const auto step = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            session.Step();
            observer.Step();
        }
    };
    step(24);
    const auto flashes = [](Machine& machine) {
        int count = 0;
        machine.scene.ForEachComponent<Gameplay::Flash>([&](Gameplay::Flash&) { ++count; });
        return count;
    };

    // The server's copy of the client's pawn.
    Gameplay::Pawn* pawn = Gameplay::Players::GetPawn(session.client.scene);
    REQUIRE(pawn);
    const Spot::NetworkId id = pawn->GetEntity().GetComponent<Spot::NetworkObject>()->GetNetworkId();
    Spot::Entity* serverPawn = session.server.system->FindEntity(id);
    REQUIRE(serverPawn);
    {
        const Gameplay::EffectLibrary::Predicted predicted(*serverPawn);
        Gameplay::EffectLibrary::Play(session.server.scene, "flash", {0.0f, 1.0f, 0.0f});
    }
    Gameplay::EffectLibrary::Play(session.server.scene, "flash", {0.0f, 1.0f, 0.0f});
    step(4);
    CHECK(flashes(session.client) == 1);
    CHECK(flashes(observer) == 2);
}

TEST_CASE("A client leaving takes its controller and pawn from the server", "[sandbox][gameplay][network]") {
    Session session;
    session.Step(12);
    REQUIRE(FindRemote(session.server.scene));
    REQUIRE(session.server.scene.FindEntity("Player"));

    session.client.session->GetHost().Disconnect(session.client.session->GetServerPeer());
    session.Step(4);
    CHECK_FALSE(FindRemote(session.server.scene));
    CHECK_FALSE(session.server.scene.FindEntity("Player"));
}

TEST_CASE("Without a session, network players do nothing and the local player plays alone",
          "[sandbox][gameplay][network]") {
    GameplayTests::World world;
    auto& mode = world.scene.CreateEntity("Rules").AddComponent<Gameplay::GameMode>();
    mode.playerPrefab = MakePlayerPrefab();
    world.scene.CreateEntity("Players").AddComponent<Gameplay::NetworkPlayers>();
    auto& player = world.scene.CreateEntity("Controller").AddComponent<Gameplay::PlayerController>();
    world.Step();
    REQUIRE(player.GetPawn());
    auto& walker = *player.GetPawn()->GetEntity().GetComponent<Walker>();
    walker.direction = {1.0f, 0.0f};
    world.Run(0.5f);
    CHECK(player.GetPawn()->GetEntity().GetTransform().GetWorldPosition().x > 1.0f);
    CHECK_FALSE(world.scene.FindComponent<Gameplay::RemoteController>());
}

TEST_CASE("Reliable requests get a player into and out of a vehicle on every machine",
          "[sandbox][gameplay][network][vehicles]") {
    Session session;
    Gameplay::VehicleSeat& seat = session.AddVehicle();
    session.Step(16);
    auto* player = Gameplay::Players::Get(session.client.scene);
    auto* character = player->GetPawn();
    REQUIRE(character);
    const Spot::EntityId characterId = character->GetEntity().GetId();
    auto* remote = FindRemote(session.server.scene);
    REQUIRE(remote);

    // Discrete actions do not depend on unreliable input packets reaching the server.
    auto& transport = dynamic_cast<Spot::ConditionedNetTransport&>(session.client.session->GetHost().GetTransport());
    transport.SetConditions({.loss = 1.0f, .duplicates = 1.0f});
    REQUIRE(character->GetEntity().GetComponent<Gameplay::Interactor>()->Interact());
    CHECK(player->GetPawn() == character);
    CHECK_FALSE(seat.IsOccupied());
    session.Step(16);

    auto* vehicle = session.client.scene.FindComponent<TestVehicle>();
    REQUIRE(vehicle);
    REQUIRE(vehicle->GetSeat()->GetOccupant() == character);
    CHECK(player->GetPawn() == vehicle);
    CHECK(remote->GetPawn() == seat.GetVehicle());
    CHECK(remote->GetPlayerPawn() == seat.GetOccupant());
    CHECK(vehicle->HasDriver());
    CHECK(character->HasState("Seated"));
    CHECK(character->HasState("Driving"));
    CHECK_FALSE(session.client.scene.GetEntity(character->viewCamera)->GetComponent<Spot::Camera>()->IsEnabled());
    CHECK(session.client.scene.GetEntity(vehicle->viewCamera)->GetComponent<Spot::Camera>()->IsEnabled());
    CHECK(Spot::Distance(character->GetEntity().GetTransform().GetWorldPosition(),
                         vehicle->GetSeat()->GetSeatPosition()) < 0.01f);
    // A client cannot bypass the server by manipulating its local seat.
    vehicle->GetSeat()->Exit();
    CHECK(vehicle->GetSeat()->IsOccupied());

    vehicle->Request("Gameplay.ExitVehicle");
    session.Step(16);
    REQUIRE(player->GetPawn());
    CHECK(player->GetPawn()->GetEntity().GetId() == characterId);
    CHECK(remote->GetPawn() == remote->GetPlayerPawn());
    CHECK_FALSE(seat.IsOccupied());
    CHECK_FALSE(vehicle->GetSeat()->IsOccupied());
    CHECK_FALSE(character->HasState("Seated"));
    CHECK_FALSE(character->HasState("Driving"));
    CHECK_FALSE(vehicle->HasDriver());
    CHECK_FALSE(vehicle->GetEntity().GetComponent<Spot::NetworkObject>()->IsOwnedLocally());
    CHECK(seat.GetVehicle()->GetEntity().GetComponent<Spot::NetworkObject>()->GetOwner() == Spot::NetPeerId::Invalid);
    CHECK(session.client.scene.GetEntity(character->viewCamera)->GetComponent<Spot::Camera>()->IsEnabled());
    CHECK_FALSE(session.client.scene.GetEntity(vehicle->viewCamera)->GetComponent<Spot::Camera>()->IsEnabled());
}

TEST_CASE("Interaction requests are checked for reach and pawn state on the server",
          "[sandbox][gameplay][network][vehicles]") {
    Session session;
    Gameplay::VehicleSeat& seat = session.AddVehicle();
    session.Step(16);
    auto* pawn = Gameplay::Players::GetPawn(session.client.scene);
    REQUIRE(pawn);
    auto* serverPawn = FindRemote(session.server.scene)->GetPawn();
    REQUIRE(serverPawn);

    SECTION("A client claiming to be near cannot interact from out of reach") {
        serverPawn->GetEntity().GetTransform().SetPosition({0.0f, 1.0f, 20.0f});
    }
    SECTION("A blocked pawn cannot interact") {
        serverPawn->SetState("Dead", true);
    }
    SECTION("An obstacle blocks interaction") {
        auto& wall = session.server.scene.CreateEntity("Wall");
        wall.GetTransform().SetPosition({0.0f, 1.0f, -1.0f});
        wall.AddComponent<Spot::BoxCollider>(Spot::Vector3{1.0f, 2.0f, 0.2f});
    }
    pawn->Request("Gameplay.Interact");
    session.Step(12);
    CHECK_FALSE(seat.IsOccupied());
    CHECK(FindRemote(session.server.scene)->GetPawn() == serverPawn);
}

TEST_CASE("Changing pawns drops queued intents and returns possession when the driver dies",
          "[sandbox][gameplay][network][vehicles]") {
    Session session;
    Gameplay::VehicleSeat& seat = session.AddVehicle();
    session.Step(16);
    auto* remote = FindRemote(session.server.scene);
    REQUIRE(remote);
    auto* character = remote->GetPawn();
    REQUIRE(character);
    auto* walker = character->GetEntity().GetComponent<Walker>();
    walker->direction = {1.0f, 0.5f};
    Spot::BitWriter writer;
    Gameplay::PawnIntents::Write(*character, writer);
    const auto bytes = writer.TakeBytes();
    remote->Receive(1000, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    REQUIRE(seat.Enter(*character));
    session.Step(12);
    CHECK(seat.GetVehicle()->GetInput().drive == 0.0f);
    CHECK(seat.GetVehicle()->GetInput().steer == 0.0f);

    character->SetState("Dead", true);
    session.Step(12);
    CHECK(remote->GetPawn() == character);
    CHECK_FALSE(seat.IsOccupied());
    CHECK_FALSE(character->HasState("Seated"));
    CHECK_FALSE(character->HasState("Driving"));
    auto* clientPawn = Gameplay::Players::GetPawn(session.client.scene);
    REQUIRE(clientPawn);
    CHECK_FALSE(dynamic_cast<Gameplay::Vehicle*>(clientPawn));
    CHECK(clientPawn->HasState("Dead"));
}

TEST_CASE("A client's driving input moves the server vehicle and stops controlling it on exit",
          "[sandbox][gameplay][network][vehicles]") {
    Session session;
    Gameplay::VehicleSeat& seat = session.AddVehicle();
    session.client.actions.Add("Accelerate", Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::W));
    session.client.actions.Add("Use", Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::E));
    session.Step(16);
    REQUIRE(seat.Enter(*FindRemote(session.server.scene)->GetPawn()));
    session.Step(12);
    auto* clientVehicle = session.client.scene.FindComponent<TestVehicle>();
    REQUIRE(clientVehicle);
    const float start = seat.GetVehicle()->GetTransform().GetWorldPosition().z;
    session.client.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::W, false});
    session.Step(20);
    CHECK(seat.GetVehicle()->GetInput().drive == Catch::Approx(1.0f));
    CHECK(seat.GetVehicle()->GetTransform().GetWorldPosition().z < start - 1.0f);
    session.client.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::W});
    session.Step(12);
    CHECK(clientVehicle->GetTransform().GetWorldPosition().z ==
          Catch::Approx(seat.GetVehicle()->GetTransform().GetWorldPosition().z).margin(0.01f));

    session.client.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::E, false});
    session.Step(12);
    REQUIRE(Gameplay::Players::GetPawn(session.client.scene));
    CHECK_FALSE(seat.IsOccupied());
    CHECK(Gameplay::Players::GetPawn(session.client.scene)->GetEntity().GetName() == "Player");
    CHECK(seat.GetVehicle()->GetInput().drive == 0.0f);
}

TEST_CASE("A driver leaving removes its character and leaves the vehicle available",
          "[sandbox][gameplay][network][vehicles]") {
    Session session;
    Gameplay::VehicleSeat& seat = session.AddVehicle();
    session.Step(16);
    auto* remote = FindRemote(session.server.scene);
    REQUIRE(remote);
    const Spot::EntityId character = remote->GetPawn()->GetEntity().GetId();
    const Spot::EntityId vehicle = seat.GetEntity().GetId();
    REQUIRE(seat.Enter(*remote->GetPawn()));
    session.Step(12);
    session.client.session->GetHost().Disconnect(session.client.session->GetServerPeer());
    session.Step(12);
    CHECK_FALSE(session.server.scene.GetEntity(character));
    CHECK_FALSE(FindRemote(session.server.scene));
    REQUIRE(session.server.scene.GetEntity(vehicle));
    CHECK_FALSE(seat.IsOccupied());
    CHECK_FALSE(seat.GetVehicle()->HasDriver());
    CHECK(seat.GetVehicle()->GetEntity().GetComponent<Spot::NetworkObject>()->GetOwner() == Spot::NetPeerId::Invalid);

    Machine newcomer;
    session.Join(newcomer);
    for (int i = 0; i < 16; ++i) {
        session.server.Step();
        newcomer.Step();
    }
    auto* next = FindRemote(session.server.scene);
    REQUIRE(next);
    REQUIRE(next->GetPawn());
    CHECK(seat.Enter(*next->GetPawn()));
}

TEST_CASE("Other clients see a driver and cannot request actions for another player's pawn",
          "[sandbox][gameplay][network][vehicles]") {
    Session session;
    Gameplay::VehicleSeat& seat = session.AddVehicle();
    session.Step(16);
    auto* remote = FindRemote(session.server.scene);
    REQUIRE(remote);
    REQUIRE(seat.Enter(*remote->GetPawn()));
    session.Step(12);

    Machine observer;
    session.Join(observer);
    for (int i = 0; i < 24; ++i) {
        session.Step();
        observer.Step();
    }
    auto* vehicle = observer.scene.FindComponent<TestVehicle>();
    REQUIRE(vehicle);
    CHECK(vehicle->HasDriver());
    REQUIRE(vehicle->GetOccupant());
    CHECK(vehicle->GetSeat()->GetOccupant() == vehicle->GetOccupant());
    CHECK(vehicle->GetOccupant()->HasState("Seated"));
    CHECK(vehicle->GetOccupant()->HasState("Driving"));
    CHECK(Gameplay::Players::GetPawn(observer.scene) != vehicle);
    auto* object = vehicle->GetEntity().GetComponent<Spot::NetworkObject>();
    CHECK_FALSE(object->IsOwnedLocally());
    observer.system->SendToServer(*object, Gameplay::PawnRequest{"Gameplay.ExitVehicle"});
    for (int i = 0; i < 12; ++i) {
        session.Step();
        observer.Step();
    }
    CHECK(seat.IsOccupied());
    CHECK(remote->GetPawn() == seat.GetVehicle());
}

TEST_CASE("The room roster replicates host and guests and removes only the leaving player",
          "[sandbox][gameplay][network]") {
    Session session;
    session.server.scene.CreateEntity("Host Controller").AddComponent<Gameplay::PlayerController>();
    session.Step(20);
    auto server = Gameplay::PlayerInfos::GetAll(session.server.scene);
    auto client = Gameplay::PlayerInfos::GetAll(session.client.scene);
    REQUIRE(server.size() == 2);
    REQUIRE(client.size() == 2);
    CHECK(server[0]->name == "Host");
    CHECK(client[0]->name == server[0]->name);
    CHECK(server[0]->IsLocal());
    CHECK_FALSE(client[0]->IsLocal());
    CHECK(client[1]->IsLocal());
    REQUIRE(Gameplay::PlayerInfos::GetLocal(session.client.scene));
    CHECK(Gameplay::PlayerInfos::GetLocal(session.client.scene)->pawn ==
          Gameplay::Players::GetPawn(session.client.scene)->GetEntity().GetId());
    server[1]->team = 2;
    server[1]->scores = {{"Score", 17}};
    session.Step(12);
    CHECK(client[1]->team == 2);
    REQUIRE(client[1]->scores.size() == 1);
    CHECK(client[1]->scores[0].name == "Score");
    CHECK(client[1]->scores[0].value == 17);
    session.client.session->GetHost().Disconnect(session.client.session->GetServerPeer());
    session.Step(8);
    server = Gameplay::PlayerInfos::GetAll(session.server.scene);
    REQUIRE(server.size() == 1);
    CHECK(server[0]->name == "Host");
}

TEST_CASE("Room admission checks names passwords and content before accepting a peer", "[sandbox][gameplay][network]") {
    Spot::LoopbackNetwork network;
    auto server = MakeHost(network);
    REQUIRE(server->Listen(9060, 4));
    Gameplay::SessionSettings settings;
    settings.password = "room password";
    constexpr std::uint64_t Content = 123456789;
    server->onConnecting = [settings](Spot::NetPeerId, std::span<const std::byte> hello) {
        return Gameplay::ValidateSessionHello(settings, Content, hello);
    };
    Gameplay::SessionHello hello{"Guest", settings.password, Content};
    bool accepted = true;
    std::string expected;
    SECTION("A matching client joins") {
    }
    SECTION("A wrong password is refused") {
        hello.password = "wrong";
        accepted = false;
        expected = "Incorrect room password";
    }
    SECTION("Different content is refused") {
        hello.content += 1;
        accepted = false;
        expected = "content differs";
    }
    SECTION("Control characters in a name are refused") {
        hello.playerName = "Guest\nFake Player";
        accepted = false;
        expected = "player name";
    }
    Spot::BitWriter writer;
    Spot::WriteArchived(writer, hello);
    auto client = MakeHost(network);
    const auto peer = client->Connect({"localhost", 9060}, writer.TakeBytes());
    REQUIRE(peer != Spot::NetPeerId::Invalid);
    bool connected = false;
    std::string refused;
    for (int i = 0; i < 8; ++i) {
        server->Poll();
        for (const auto& event : client->Poll()) {
            connected = connected || event.type == Spot::NetEvent::Type::Connected;
            if (event.type == Spot::NetEvent::Type::Disconnected) {
                refused = event.reason;
            }
        }
    }
    CHECK(connected == accepted);
    if (!accepted) {
        CHECK(refused.find(expected) != std::string::npos);
    }
}
