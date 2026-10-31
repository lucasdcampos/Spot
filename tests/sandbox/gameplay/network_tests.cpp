#include "gameplay_world.h"

#include "gameplay/core/interactor.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/pawn_request.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/core/player_input.h"
#include "gameplay/core/player_name.h"
#include "gameplay/core/players.h"
#include "gameplay/effects/effect_library.h"
#include "gameplay/effects/flash.h"
#include "gameplay/game/game_mode.h"
#include "gameplay/game/spawn_point.h"
#include "gameplay/network/network_players.h"
#include "gameplay/network/pawn_intents.h"
#include "gameplay/network/player_info.h"
#include "gameplay/network/remote_controller.h"
#include "gameplay/network/server_list.h"
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

// A pawn as fast as a Walker, unless a test lets it go faster.
class TestPawn final : public Gameplay::Pawn {
public:
    float limit = 4.0f;
    float GetSpeedLimit() const override { return limit; }
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

// Its player's machine decides where it is when ownerDecided, as the sandbox's player; else the server does.
Spot::AssetRef<Spot::Prefab> MakePlayerPrefab(bool ownerDecided = false) {
    // Before any world does, as the prefab saves the game's components.
    Spot::Detail::LoadGameCode();
    Spot::RegisterComponent<Walker>("Tests.Walker");
    Spot::RegisterComponent<TestPawn>("Tests.NetworkPawn");
    Spot::Scene authoring;
    Spot::Entity& player = authoring.CreateEntity("Player");
    player.AddComponent<Spot::NetworkObject>().authority =
        ownerDecided ? Spot::NetworkAuthority::Owner : Spot::NetworkAuthority::Server;
    player.AddComponent<Spot::NetworkTransform>().maxExtrapolation = 0.0f;
    AddCamera(player.AddComponent<TestPawn>());
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
    bool ownerDecided = false;
    Spot::LoopbackNetwork network;
    Spot::AssetRef<Spot::Prefab> prefab = MakePlayerPrefab(ownerDecided);
    Spot::AssetRef<Spot::Prefab> vehiclePrefab = MakeVehiclePrefab();
    Spot::AssetRef<Spot::Prefab> infoPrefab = MakeInfoPrefab();
    Machine server;
    Machine client;

    explicit Session(bool ownerDecidedPlayer = false) : ownerDecided(ownerDecidedPlayer) {
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

TEST_CASE("A pawn its player's machine decides moves there at once, and the server shows it",
          "[sandbox][gameplay][network]") {
    Session session(true);
    session.Step(12);
    Gameplay::Pawn* pawn = Gameplay::Players::GetPawn(session.client.scene);
    REQUIRE(pawn);
    REQUIRE(pawn->GetEntity().GetComponent<Spot::NetworkObject>()->IsDecidedByClient());
    Spot::Entity& serverBody = FindRemote(session.server.scene)->GetPawn()->GetEntity();
    REQUIRE(serverBody.GetComponent<Spot::NetworkObject>()->IsDecidedByClient());
    Spot::Entity& clientBody = pawn->GetEntity();
    const Spot::Vector3 start = clientBody.GetTransform().GetWorldPosition();
    CHECK(Spot::Distance(start, serverBody.GetTransform().GetWorldPosition()) < 0.01f);

    // The player's machine does not wait for the server.
    auto& walker = *clientBody.GetComponent<Walker>();
    walker.direction = {1.0f, 0.0f};
    session.client.Step();
    CHECK(clientBody.GetTransform().GetWorldPosition().x > start.x + 0.05f);
    CHECK(serverBody.GetTransform().GetWorldPosition().x == Catch::Approx(start.x).margin(0.001f));

    session.Step(30);
    walker.direction = {};
    session.Step(12);
    const Spot::Vector3 moved = clientBody.GetTransform().GetWorldPosition();
    CHECK(moved.x > start.x + 1.5f);
    CHECK(Spot::Distance(serverBody.GetTransform().GetWorldPosition(), moved) < 0.01f);
    CHECK(session.server.scene.FindComponent<Gameplay::NetworkPlayers>()->GetCorrections() == 0);

    SECTION("The server teleporting it puts it there on its player's machine, which walks on from there") {
        serverBody.GetComponent<Gameplay::Pawn>()->Teleport(moved + Spot::Vector3{0.0f, 0.0f, 6.0f}, {}, {});
        session.Step(6);
        CHECK(Spot::Distance(clientBody.GetTransform().GetWorldPosition(), moved + Spot::Vector3{0.0f, 0.0f, 6.0f}) <
              0.01f);
        walker.direction = {1.0f, 0.0f};
        session.Step(20);
        walker.direction = {};
        session.Step(12);
        const Spot::Vector3 walked = clientBody.GetTransform().GetWorldPosition();
        CHECK(walked.x > moved.x + 1.0f);
        CHECK(walked.z == Catch::Approx(moved.z + 6.0f).margin(0.01f));
        CHECK(Spot::Distance(serverBody.GetTransform().GetWorldPosition(), walked) < 0.01f);
        CHECK(session.server.scene.FindComponent<Gameplay::NetworkPlayers>()->GetCorrections() == 0);
    }

    SECTION("Going fast while it may, then slowing down, it is not put back") {
        auto& serverPawn = *serverBody.GetComponent<TestPawn>();
        serverPawn.limit = 20.0f;
        for (int i = 0; i < 60; ++i) {
            clientBody.GetTransform().Translate({0.25f, 0.0f, 0.0f});
            session.Step();
        }
        serverPawn.limit = 4.0f;
        session.Step(60);
        CHECK(session.server.scene.FindComponent<Gameplay::NetworkPlayers>()->GetCorrections() == 0);
        CHECK(clientBody.GetTransform().GetWorldPosition().x > moved.x + 14.0f);
    }

    SECTION("Flying through everything, a cheat, it is put back") {
        session.Step(60);
        serverBody.GetComponent<Gameplay::Pawn>()->SetState("Noclip", true);
        session.Step(2);
        CHECK(session.server.scene.FindComponent<Gameplay::NetworkPlayers>()->GetCorrections() >= 1);
    }

    SECTION("Moving faster than the pawn may, it is put back") {
        // Where it was a second before, it stood still.
        session.Step(60);
        clientBody.GetTransform().Translate({30.0f, 0.0f, 0.0f});
        session.Step(12);
        CHECK(session.server.scene.FindComponent<Gameplay::NetworkPlayers>()->GetCorrections() == 1);
        CHECK(Spot::Distance(clientBody.GetTransform().GetWorldPosition(), moved) < 0.01f);
        CHECK(Spot::Distance(serverBody.GetTransform().GetWorldPosition(), moved) < 0.01f);
    }
}

TEST_CASE("Effects a deciding player already showed are not sent back to that player", "[sandbox][gameplay][network]") {
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
    CHECK(Gameplay::IsValidPlayerName(server[1]->name));
    CHECK(client[1]->name == server[1]->name);
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
    SECTION("Spaces in a nickname are refused") {
        hello.playerName = "Guest Player";
        accepted = false;
        expected = "without spaces";
    }
    SECTION("Unicode spaces in a nickname are refused") {
        hello.playerName = "Guest\xc2\xa0Player";
        accepted = false;
        expected = "without spaces";
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

TEST_CASE("Rooms on the local network are found with their address, players and password",
          "[sandbox][gameplay][network]") {
    Spot::LoopbackNetwork network;
    const auto room = [&](Gameplay::ServerInfo info) {
        auto responder = std::make_unique<Spot::NetDiscoveryResponder>(network.CreateDatagramSocket(), 5);
        REQUIRE(responder->Listen(Gameplay::LanServers::DefaultPort));
        responder->answer = [info]() mutable {
            Spot::BitWriter writer;
            Spot::WriteArchived(writer, info);
            return writer.TakeBytes();
        };
        return responder;
    };
    auto open =
        room({.name = "Open", .map = "Playground", .mode = "Free", .players = 3, .maxPlayers = 16, .port = 27015});
    auto locked =
        room({.name = "Locked", .map = "Empty", .players = 1, .maxPlayers = 4, .password = true, .port = 27100});
    // Another game's, or another version's, is not listed.
    Spot::NetDiscoveryResponder other(network.CreateDatagramSocket(), 6);
    REQUIRE(other.Listen(Gameplay::LanServers::DefaultPort));
    other.answer = [] { return std::vector<std::byte>(4); };

    Gameplay::LanServers servers(5, Gameplay::LanServers::DefaultPort, network.CreateDatagramSocket());
    servers.Refresh();
    REQUIRE(servers.IsAvailable());
    open->Update();
    locked->Update();
    other.Update();
    servers.Update();
    const auto& found = servers.GetServers();
    REQUIRE(found.size() == 2);
    CHECK(found[0].info.name == "Locked");
    CHECK(found[0].info.password);
    CHECK(found[0].address.ends_with(":27100"));
    CHECK(found[1].info.name == "Open");
    CHECK(found[1].info.players == 3);
    CHECK(found[1].info.maxPlayers == 16);
    CHECK(found[1].info.mode == "Free");
    CHECK(found[1].address.ends_with(":27015"));
    // The address joins the room's game port, on the machine that answered.
    CHECK(Spot::NetAddress::Parse(found[1].address, 0)->port == 27015);
}
