#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace {

class Door final : public Spot::Component, public Spot::Replicated {
public:
    bool open = false;
    std::string label;
    Spot::EntityId target = Spot::EntityId::Invalid;
    int replications = 0;

    void Replicate(Spot::Archive& archive) override {
        archive.Field("open", open);
        archive.Field("label", label);
        archive.Field("target", target);
    }
    void OnReplicated() override { ++replications; }
    void Serialize(Spot::Archive& archive) override { archive.Field("label", label); }
};

struct Knock {
    static constexpr std::string_view NetName = "Tests.Knock";
    int strength = 0;
    Spot::EntityId who = Spot::EntityId::Invalid;

    void Serialize(Spot::Archive& archive) {
        archive.Field("strength", strength);
        archive.Field("who", who);
    }
};

const Spot::AssetId SceneAsset{1, 2};
const Spot::AssetId CratePrefab{3, 4};

void RegisterTestComponents() {
    Spot::RegisterComponent<Door>("Tests.Door");
}

Spot::AssetRef<Spot::Prefab> MakeCratePrefab() {
    RegisterTestComponents();
    Spot::Scene authoring;
    Spot::Entity& crate = authoring.CreateEntity("Crate");
    crate.AddComponent<Spot::NetworkObject>();
    crate.AddComponent<Spot::NetworkTransform>().maxExtrapolation = 0.0f;
    crate.AddComponent<Door>().label = "crate";
    Spot::Entity& lid = crate.CreateChild("Lid");
    lid.AddComponent<Spot::NetworkObject>();
    lid.AddComponent<Door>().label = "lid";
    auto prefab = Spot::Prefab::FromString(crate.SaveToString());
    REQUIRE(prefab);
    return Spot::AssetRef<Spot::Prefab>(CratePrefab, std::make_shared<const Spot::Prefab>(std::move(*prefab)));
}

// The same level on every machine, loaded from the same file: a door that replicates its state, and a box that moves.
std::string MakeLevel() {
    RegisterTestComponents();
    Spot::Scene authoring;
    authoring.CreateEntity("Ground");
    Spot::Entity& door = authoring.CreateEntity("Door");
    door.AddComponent<Spot::NetworkObject>();
    door.AddComponent<Door>();
    Spot::Entity& box = authoring.CreateEntity("Box");
    box.AddComponent<Spot::NetworkObject>();
    box.AddComponent<Spot::NetworkTransform>().maxExtrapolation = 0.0f;
    box.AddComponent<Spot::BoxCollider>();
    box.AddComponent<Spot::Rigidbody>();
    Spot::Entity& gone = authoring.CreateEntity("Gone");
    gone.AddComponent<Spot::NetworkObject>();
    return authoring.SaveToString();
}

void BuildLevel(Spot::Scene& scene) {
    static const std::string level = MakeLevel();
    REQUIRE(scene.LoadFromString(level));
    scene.SetSourceAsset(SceneAsset);
}

struct Machine {
    std::shared_ptr<Spot::NetworkSession> session;
    Spot::Scene scene;
    Spot::NetworkSystem* system = nullptr;
    // Shown at once, unless a test lets snapshots arriving slowly push what is shown back.
    float maxInterpolationDelay = 0.0f;

    void Start(std::unique_ptr<Spot::NetHost> host, const Spot::AssetRef<Spot::Prefab>& prefab,
               std::size_t snapshotBudget = 4000) {
        session = std::make_shared<Spot::NetworkSession>(std::move(host));
        BuildLevel(scene);
        Spot::NetworkSystemConfig config{.interpolationDelay = 0.0f, .maxInterpolationDelay = maxInterpolationDelay};
        config.snapshotBudget = snapshotBudget;
        config.loadPrefab = [prefab](Spot::AssetId id) {
            return id == CratePrefab ? prefab : Spot::AssetRef<Spot::Prefab>();
        };
        system = &scene.AddSystem<Spot::NetworkSystem>(session, config);
    }
};

struct World {
    Spot::LoopbackNetwork network;
    Spot::AssetRef<Spot::Prefab> prefab = MakeCratePrefab();
    Machine server, client;

    explicit World(std::size_t snapshotBudget = 4000) {
        auto serverHost = std::make_unique<Spot::NetHost>(
            network.CreateTransport(), Spot::NetHostConfig{.channels = Spot::NetworkSession::ChannelCount});
        REQUIRE(serverHost->Listen(7000, 8));
        server.Start(std::move(serverHost), prefab, snapshotBudget);
    }

    void Join(Machine& machine) {
        auto host = std::make_unique<Spot::NetHost>(
            network.CreateTransport(), Spot::NetHostConfig{.channels = Spot::NetworkSession::ChannelCount});
        REQUIRE(host->Connect({"localhost", 7000}) != Spot::NetPeerId::Invalid);
        machine.Start(std::move(host), prefab);
    }

    void Step(int frames = 1) {
        for (int i = 0; i < frames; ++i) {
            server.scene.Update(1.0f / 60.0f);
            client.scene.Update(1.0f / 60.0f);
        }
    }

    Spot::Entity& Find(Machine& machine, std::string_view name) {
        Spot::Entity* entity = machine.scene.FindEntity(name);
        REQUIRE(entity);
        return *entity;
    }
};

} // namespace

TEST_CASE("A client's scene follows the server's objects", "[network]") {
    World world;
    world.Join(world.client);
    world.Step(10);
    REQUIRE(world.client.system->IsSynchronized());
    const std::vector<Spot::NetPeerId> peers = world.server.system->GetReadyPeers();
    REQUIRE(peers.size() == 1);

    Spot::Entity& serverBox = world.Find(world.server, "Box");
    Spot::Entity& clientBox = world.Find(world.client, "Box");
    CHECK(serverBox.GetComponent<Spot::NetworkObject>()->GetNetworkId() ==
          clientBox.GetComponent<Spot::NetworkObject>()->GetNetworkId());
    CHECK(serverBox.GetComponent<Spot::NetworkObject>()->HasAuthority());
    CHECK_FALSE(clientBox.GetComponent<Spot::NetworkObject>()->HasAuthority());
    CHECK(clientBox.GetComponent<Spot::Rigidbody>()->IsKinematic());
    CHECK(Spot::Network::IsServer(world.server.scene));
    CHECK(Spot::Network::IsClient(world.client.scene));
    CHECK_FALSE(Spot::Network::HasAuthority(clientBox));
    CHECK(Spot::Network::HasAuthority(world.Find(world.client, "Ground")));

    serverBox.GetComponent<Spot::Rigidbody>()->SetKinematic(true);
    serverBox.GetTransform().SetWorldPosition({3.0f, 4.0f, 5.0f});
    serverBox.GetTransform().SetWorldRotation(Spot::Quaternion::FromEuler({0.0f, 1.0f, 0.0f}));
    auto& serverDoor = *world.Find(world.server, "Door").GetComponent<Door>();
    serverDoor.open = true;
    serverDoor.label = "front";
    serverDoor.target = serverBox.GetId();
    world.Step(10);

    const Spot::Vector3 position = clientBox.GetTransform().GetWorldPosition();
    CHECK(position.x == Catch::Approx(3.0f).margin(0.002f));
    CHECK(position.y == Catch::Approx(4.0f).margin(0.002f));
    CHECK(position.z == Catch::Approx(5.0f).margin(0.002f));
    auto& clientDoor = *world.Find(world.client, "Door").GetComponent<Door>();
    CHECK(clientDoor.open);
    CHECK(clientDoor.label == "front");
    // The reference arrives as the client's own entity.
    CHECK(clientDoor.target == clientBox.GetId());
    CHECK(clientDoor.replications >= 1);

    SECTION("Unchanged objects are not sent again") {
        const int replications = clientDoor.replications;
        world.Step(20);
        CHECK(clientDoor.replications == replications);
    }
}

TEST_CASE("Objects spawned from prefabs appear and disappear on clients", "[network]") {
    World world;
    world.Join(world.client);
    world.Step(6);

    Spot::Entity* crate = world.server.scene.Instantiate(world.prefab);
    REQUIRE(crate);
    crate->GetTransform().SetWorldPosition({-2.0f, 1.0f, 0.0f});
    crate->GetChildren()[0]->GetComponent<Door>()->open = true;
    world.Step(10);

    Spot::Entity* copy = world.client.scene.FindEntity("Crate");
    REQUIRE(copy);
    CHECK(copy->GetComponent<Spot::NetworkObject>()->GetNetworkId() ==
          crate->GetComponent<Spot::NetworkObject>()->GetNetworkId());
    CHECK(copy->GetTransform().GetWorldPosition().x == Catch::Approx(-2.0f).margin(0.002f));
    Spot::Entity* lid = copy->FindChild("Lid");
    REQUIRE(lid);
    CHECK(lid->GetComponent<Spot::NetworkObject>()->GetNetworkId() ==
          crate->GetChildren()[0]->GetComponent<Spot::NetworkObject>()->GetNetworkId());
    CHECK(lid->GetComponent<Door>()->open);

    crate->Destroy();
    world.Step(10);
    CHECK_FALSE(world.client.scene.FindEntity("Crate"));
}

TEST_CASE("Entities of a scene file keep the ids they were saved with", "[network]") {
    Spot::Scene first, second;
    BuildLevel(first);
    BuildLevel(second);
    Spot::Entity* door = first.FindEntity("Door");
    REQUIRE(door);
    CHECK(door->GetSavedId() != 0);
    CHECK(door->GetSavedId() == second.FindEntity("Door")->GetSavedId());
    CHECK(door->GetId() != second.FindEntity("Door")->GetId());
    CHECK(first.CreateEntity("Made").GetSavedId() == 0);
}

TEST_CASE("Clients joining late get the scene as the server has it", "[network]") {
    World world;
    world.Find(world.server, "Gone").Destroy();
    world.Find(world.server, "Door").GetComponent<Door>()->label = "changed before";
    REQUIRE(world.server.scene.Instantiate(world.prefab));
    world.Step(3);

    world.Join(world.client);
    world.Step(12);
    CHECK_FALSE(world.client.scene.FindEntity("Gone"));
    CHECK(world.Find(world.client, "Door").GetComponent<Door>()->label == "changed before");
    CHECK(world.client.scene.FindEntity("Crate"));
}

TEST_CASE("A client running another scene gets none of the server's objects", "[network]") {
    World world;
    world.Join(world.client);
    world.client.scene.SetSourceAsset(Spot::AssetId{9, 9});
    world.Step(10);
    CHECK(world.server.system->GetReadyPeers().empty());
    CHECK_FALSE(world.client.system->IsSynchronized());
}

TEST_CASE("Clients show objects farther back while snapshots come less often", "[network]") {
    World world;
    world.client.maxInterpolationDelay = 0.5f;
    world.Join(world.client);
    world.Step(12);
    REQUIRE(world.client.system->IsSynchronized());
    const double prompt = world.client.system->GetShownDelay();

    // A server whose frames are slow sends a snapshot every few of the client's.
    Spot::Entity& serverBox = world.Find(world.server, "Box");
    for (int frame = 0; frame < 60; ++frame) {
        if (frame % 4 == 0) {
            serverBox.GetTransform().Translate({0.1f, 0.0f, 0.0f});
            world.server.scene.Update(4.0f / 60.0f);
        }
        world.client.scene.Update(1.0f / 60.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const double slow = world.client.system->GetShownDelay();
    CHECK(slow > prompt);
    // At least the time between snapshots, so a state is always ahead of what is shown.
    CHECK(slow >= 0.02);
}

TEST_CASE("Objects a client owns go from the client to the server", "[network]") {
    World world;
    Spot::Entity& serverBox = world.Find(world.server, "Box");
    serverBox.GetComponent<Spot::NetworkObject>()->authority = Spot::NetworkAuthority::Owner;
    world.Join(world.client);
    world.Find(world.client, "Box").GetComponent<Spot::NetworkObject>()->authority = Spot::NetworkAuthority::Owner;
    world.Step(6);
    const Spot::NetPeerId client = world.server.system->GetReadyPeers().at(0);
    world.server.system->SetOwner(*serverBox.GetComponent<Spot::NetworkObject>(), client);
    world.Step(6);

    Spot::Entity& clientBox = world.Find(world.client, "Box");
    REQUIRE(clientBox.GetComponent<Spot::NetworkObject>()->IsOwnedLocally());
    CHECK(clientBox.GetComponent<Spot::NetworkObject>()->HasAuthority());
    CHECK_FALSE(serverBox.GetComponent<Spot::NetworkObject>()->HasAuthority());
    CHECK_FALSE(clientBox.GetComponent<Spot::Rigidbody>()->IsKinematic());
    CHECK(serverBox.GetComponent<Spot::Rigidbody>()->IsKinematic());

    clientBox.GetComponent<Spot::Rigidbody>()->SetKinematic(true);
    clientBox.GetTransform().SetWorldPosition({7.0f, 0.0f, 0.0f});
    world.Step(10);
    CHECK(serverBox.GetTransform().GetWorldPosition().x == Catch::Approx(7.0f).margin(0.002f));

    SECTION("Leaving gives it back to the server") {
        world.client.session->GetHost().Disconnect(world.client.session->GetServerPeer());
        world.Step(4);
        CHECK(serverBox.GetComponent<Spot::NetworkObject>()->HasAuthority());
    }

    SECTION("Given back, it goes on the way it was moving") {
        for (int i = 0; i < 20; ++i) {
            clientBox.GetTransform().Translate({0.1f, 0.0f, 0.0f});
            world.Step();
        }
        world.server.system->SetOwner(*serverBox.GetComponent<Spot::NetworkObject>(), Spot::NetPeerId::Invalid);
        const auto* body = serverBox.GetComponent<Spot::Rigidbody>();
        CHECK_FALSE(body->IsKinematic());
        CHECK(body->GetLinearVelocity().x > 0.0f);
    }
}

TEST_CASE("Messages reach objects and sessions with entity references", "[network]") {
    World world;
    world.Join(world.client);
    world.Step(10);

    Spot::Entity& serverDoor = world.Find(world.server, "Door");
    Spot::Entity& clientDoor = world.Find(world.client, "Door");
    int serverKnocks = 0;
    Spot::NetPeerId from = Spot::NetPeerId::Invalid;
    world.server.system->OnObjectMessage<Knock>([&](Spot::NetworkObject& object, Spot::NetPeerId sender, Knock& knock) {
        CHECK(&object.GetEntity() == &serverDoor);
        CHECK(knock.who == world.Find(world.server, "Box").GetId());
        serverKnocks += knock.strength;
        from = sender;
    });
    int clientKnocks = 0;
    world.client.system->OnObjectMessage<Knock>(
        [&](Spot::NetworkObject&, Spot::NetPeerId, Knock& knock) { clientKnocks += knock.strength; });
    std::string greeting;
    world.client.session->GetMessages().On<Knock>([&](Spot::NetPeerId, Knock& knock) {
        greeting = std::to_string(knock.strength);
        CHECK(knock.who == world.Find(world.client, "Box").GetId());
    });

    world.client.system->SendToServer(*clientDoor.GetComponent<Spot::NetworkObject>(),
                                      Knock{.strength = 3, .who = world.Find(world.client, "Box").GetId()});
    world.server.system->SendToClients(*serverDoor.GetComponent<Spot::NetworkObject>(), Knock{.strength = 5},
                                       Spot::NetDelivery::Unreliable);
    world.server.session->SendToAll(Knock{.strength = 8, .who = world.Find(world.server, "Box").GetId()});
    world.Step(4);
    CHECK(serverKnocks == 3);
    CHECK(from == world.server.system->GetReadyPeers().at(0));
    CHECK(clientKnocks == 5);
    CHECK(greeting == "8");
}

TEST_CASE("Replication gets through a lossy, slow network", "[network]") {
    World world;
    auto conditioned = std::make_unique<Spot::ConditionedNetTransport>(
        world.network.CreateTransport(), Spot::NetConditions{.latency = 0.0f, .loss = 0.5f}, std::function<double()>(),
        7u);
    auto host = std::make_unique<Spot::NetHost>(std::move(conditioned),
                                                Spot::NetHostConfig{.channels = Spot::NetworkSession::ChannelCount});
    REQUIRE(host->Connect({"localhost", 7000}) != Spot::NetPeerId::Invalid);
    world.client.Start(std::move(host), world.prefab);
    world.Step(20);

    for (int i = 0; i < 5; ++i) {
        REQUIRE(world.server.scene.Instantiate(world.prefab));
    }
    world.Find(world.server, "Door").GetComponent<Door>()->label = "through loss";
    world.Step(120);
    std::size_t crates = 0;
    for (Spot::Entity* root : world.client.scene.GetRootEntities()) {
        crates += root->GetName() == "Crate" ? 1 : 0;
    }
    CHECK(crates == 5);
    CHECK(world.Find(world.client, "Door").GetComponent<Door>()->label == "through loss");
}

TEST_CASE("A scene whose session ended plays alone", "[network]") {
    World world;
    world.Join(world.client);
    world.Step(6);
    Spot::Entity& clientBox = world.Find(world.client, "Box");
    REQUIRE(clientBox.GetComponent<Spot::Rigidbody>()->IsKinematic());

    world.client.system->SetSession(nullptr);
    CHECK_FALSE(Spot::Network::Get(world.client.scene));
    CHECK(Spot::Network::IsServer(world.client.scene));
    CHECK(clientBox.GetComponent<Spot::NetworkObject>()->HasAuthority());
    CHECK_FALSE(clientBox.GetComponent<Spot::Rigidbody>()->IsKinematic());
    world.Step(3);
}

TEST_CASE("References to later spawns resolve even across snapshot budgets", "[network]") {
    // With one byte as the budget, at most one object's state fits in each snapshot.
    World world(1);
    auto* first = world.server.scene.Instantiate(world.prefab);
    auto* second = world.server.scene.Instantiate(world.prefab);
    REQUIRE(first);
    REQUIRE(second);
    first->GetComponent<Door>()->target = second->GetId();
    second->GetComponent<Door>()->target = first->GetId();
    world.Find(world.server, "Door").GetComponent<Door>()->target = first->GetId();
    world.Step(2);
    const auto firstId = first->GetComponent<Spot::NetworkObject>()->GetNetworkId();
    const auto secondId = second->GetComponent<Spot::NetworkObject>()->GetNetworkId();
    world.Join(world.client);
    world.Step(80);

    const auto* firstCopy = world.client.system->FindEntity(firstId);
    const auto* secondCopy = world.client.system->FindEntity(secondId);
    REQUIRE(firstCopy);
    REQUIRE(secondCopy);
    CHECK(world.Find(world.client, "Door").GetComponent<Door>()->target == firstCopy->GetId());
    CHECK(firstCopy->GetComponent<Door>()->target == secondCopy->GetId());
    CHECK(secondCopy->GetComponent<Door>()->target == firstCopy->GetId());
    const int replications = firstCopy->GetComponent<Door>()->replications;
    world.Step(20);
    CHECK(firstCopy->GetComponent<Door>()->replications == replications);
}

TEST_CASE("A return to the acknowledged state is sent despite lost acknowledgements", "[network]") {
    World world;
    auto conditioned = std::make_unique<Spot::ConditionedNetTransport>(world.network.CreateTransport());
    auto& conditions = *conditioned;
    auto host = std::make_unique<Spot::NetHost>(std::move(conditioned),
                                                Spot::NetHostConfig{.channels = Spot::NetworkSession::ChannelCount});
    REQUIRE(host->Connect({"localhost", 7000}) != Spot::NetPeerId::Invalid);
    world.client.Start(std::move(host), world.prefab);
    world.Step(16);
    auto& server = world.Find(world.server, "Door");
    auto& client = world.Find(world.client, "Door");
    conditions.SetConditions({.loss = 1.0f});
    server.GetComponent<Door>()->open = true;
    world.server.system->SetOwner(*server.GetComponent<Spot::NetworkObject>(),
                                  world.server.system->GetReadyPeers().at(0));
    world.Step(12);
    REQUIRE(client.GetComponent<Door>()->open);
    REQUIRE(client.GetComponent<Spot::NetworkObject>()->IsOwnedLocally());

    server.GetComponent<Door>()->open = false;
    world.server.system->SetOwner(*server.GetComponent<Spot::NetworkObject>(), Spot::NetPeerId::Invalid);
    world.Step(12);
    CHECK_FALSE(client.GetComponent<Door>()->open);
    CHECK_FALSE(client.GetComponent<Spot::NetworkObject>()->IsOwnedLocally());

    conditions.SetConditions({});
    world.Step(16);
    const int replications = client.GetComponent<Door>()->replications;
    world.Step(16);
    CHECK(client.GetComponent<Door>()->replications == replications);
}

TEST_CASE("A stopped transform ends extrapolation even after its last moving pose was acknowledged", "[network]") {
    World world;
    world.Join(world.client);
    world.Step(12);
    auto& server = world.Find(world.server, "Box");
    auto& client = world.Find(world.client, "Box");
    server.GetComponent<Spot::Rigidbody>()->SetKinematic(true);
    client.GetComponent<Spot::NetworkTransform>()->maxExtrapolation = 0.25f;
    for (int i = 1; i <= 8; ++i) {
        server.GetTransform().SetWorldPosition({static_cast<float>(i) * 0.1f, 0.0f, 0.0f});
        world.Step(2);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    world.Step(12);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    world.Step(4);
    CHECK(client.GetTransform().GetWorldPosition().x == Catch::Approx(0.8f).margin(0.002f));
    CHECK(client.GetTransform().GetWorldPosition().y == Catch::Approx(0.0f).margin(0.002f));
}

TEST_CASE("Clients count the moving objects shown past their last state, as when snapshots stop", "[network]") {
    World world;
    world.Join(world.client);
    world.Step(12);
    auto& server = world.Find(world.server, "Box");
    server.GetComponent<Spot::Rigidbody>()->SetKinematic(true);
    for (int i = 1; i <= 8; ++i) {
        server.GetTransform().SetWorldPosition({static_cast<float>(i) * 0.1f, 0.0f, 0.0f});
        world.Step(2);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const Spot::NetworkSystem::Statistics& statistics = world.client.system->GetStatistics();
    CHECK(statistics.shownObjects > 0);

    // The server stops sending while the box moves: the client is left past its last state.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    world.client.scene.Update(1.0f / 60.0f);
    CHECK(statistics.lateObjects == 1);

    // Stopped, it is at rest at its last state, not late.
    world.Step(12);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    world.Step(2);
    CHECK(statistics.lateObjects == 0);
}
