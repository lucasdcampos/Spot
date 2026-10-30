#include "gameplay_world.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/core/player_input.h"
#include "gameplay/core/players.h"
#include "gameplay/game/game_mode.h"
#include "gameplay/game/spawn_point.h"
#include "gameplay/network/network_players.h"
#include "gameplay/network/remote_controller.h"

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

const Spot::AssetId PlayerPrefab{11, 22};

Spot::AssetRef<Spot::Prefab> MakePlayerPrefab() {
    // Before any world does, as the prefab saves the game's components.
    Spot::Detail::LoadGameCode();
    Spot::RegisterComponent<Walker>("Tests.Walker");
    Spot::Scene authoring;
    Spot::Entity& player = authoring.CreateEntity("Player");
    player.AddComponent<Spot::NetworkObject>();
    player.AddComponent<Spot::NetworkTransform>().maxExtrapolation = 0.0f;
    player.AddComponent<Gameplay::Pawn>();
    player.AddComponent<Walker>();
    auto prefab = Spot::Prefab::FromString(player.SaveToString());
    REQUIRE(prefab);
    return Spot::AssetRef<Spot::Prefab>(PlayerPrefab, std::make_shared<const Spot::Prefab>(std::move(*prefab)));
}

struct Machine : GameplayTests::World {
    std::shared_ptr<Spot::NetworkSession> session;
    Spot::NetworkSystem* system = nullptr;

    void Start(std::unique_ptr<Spot::NetHost> host, const Spot::AssetRef<Spot::Prefab>& prefab) {
        session = std::make_shared<Spot::NetworkSession>(std::move(host));
        scene.CreateEntity("Players").AddComponent<Gameplay::NetworkPlayers>();
        Spot::NetworkSystemConfig config{.interpolationDelay = 0.0f};
        config.loadPrefab = [prefab](Spot::AssetId id) {
            return id == PlayerPrefab ? prefab : Spot::AssetRef<Spot::Prefab>();
        };
        system = &scene.AddSystem<Spot::NetworkSystem>(session, config);
    }
};

std::unique_ptr<Spot::NetHost> MakeHost(Spot::LoopbackNetwork& network) {
    return std::make_unique<Spot::NetHost>(network.CreateTransport(),
                                           Spot::NetHostConfig{.channels = Spot::NetworkSession::ChannelCount});
}

struct Session {
    Spot::LoopbackNetwork network;
    Spot::AssetRef<Spot::Prefab> prefab = MakePlayerPrefab();
    Machine server;
    Machine client;

    Session() {
        // A dedicated server: its rules, and spawn points, but no player of its own.
        auto& mode = server.scene.CreateEntity("Rules").AddComponent<Gameplay::GameMode>();
        mode.playerPrefab = prefab;
        server.scene.CreateEntity("Spawn").AddComponent<Gameplay::SpawnPoint>();
        auto host = MakeHost(network);
        REQUIRE(host->Listen(9000, 4));
        server.Start(std::move(host), prefab);

        client.scene.CreateEntity("Controller").AddComponent<Gameplay::PlayerController>();
        auto join = MakeHost(network);
        REQUIRE(join->Connect({"localhost", 9000}) != Spot::NetPeerId::Invalid);
        client.Start(std::move(join), prefab);
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
