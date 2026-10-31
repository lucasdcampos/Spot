#include "gameplay_world.h"
#include "registered_assets.h"
#include "temp_directory.h"

#include "gameplay/core/world_time.h"
#include "gameplay/game/game_catalog.h"
#include "gameplay/game/game_mode.h"
#include "gameplay/game/map_info.h"
#include "gameplay/menus/menu.h"

#include <spot_import.h>

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <memory>

namespace {

class TestMode final : public Gameplay::GameMode {
public:
    inline static int s_starts = 0;

protected:
    void OnStart() override {
        ++s_starts;
        GameMode::OnStart();
    }
};

const Spot::AssetId ModeId{71, 72};

Spot::AssetRef<Spot::Prefab> MakeMode(Spot::AssetId id = ModeId, std::string name = "Test Mode", bool networked = true,
                                      bool active = true) {
    Spot::Detail::LoadGameCode();
    Spot::RegisterComponent<TestMode>("Tests.MapMode");
    Spot::Scene scene;
    auto& root = scene.CreateEntity(name);
    root.SetActive(active);
    root.AddComponent<TestMode>().name = std::move(name);
    if (networked) {
        root.AddComponent<Spot::NetworkObject>();
    }
    auto prefab = Spot::Prefab::FromString(root.SaveToString());
    REQUIRE(prefab);
    return {id, std::make_shared<const Spot::Prefab>(std::move(*prefab))};
}

struct RestoreTime {
    float scale = Spot::Time::GetTimeScale();
    ~RestoreTime() { Spot::Time::SetTimeScale(scale); }
};

std::unique_ptr<Spot::NetHost> MakeHost(Spot::LoopbackNetwork& network) {
    return std::make_unique<Spot::NetHost>(network.CreateTransport(),
                                           Spot::NetHostConfig{.channels = Spot::NetworkSession::ChannelCount});
}

} // namespace

TEST_CASE("A map starts its mode from a prefab exactly once", "[sandbox][gameplay][maps]") {
    GameplayTests::World world;
    auto& map = world.scene.CreateEntity("Map").AddComponent<Gameplay::MapInfo>();
    map.defaultMode = MakeMode();
    std::string expectedName = "Test Mode";
    Spot::AssetId expectedId = ModeId;
    SECTION("The map's default mode") {
    }
    SECTION("A different mode selected before starting") {
        expectedName = "Selected Mode";
        expectedId = {81, 82};
        REQUIRE(map.StartMode(MakeMode(expectedId, expectedName)));
    }
    TestMode::s_starts = 0;
    world.Step();
    REQUIRE(Gameplay::GameMode::Get(world.scene));
    CHECK(Gameplay::GameMode::Get(world.scene)->name == expectedName);
    CHECK(Gameplay::GameMode::Get(world.scene)->GetEntity().GetPrefabAssetId() == expectedId);
    world.Step();
    CHECK(TestMode::s_starts == 1);
    CHECK_FALSE(map.StartMode(map.defaultMode));
    CHECK(map.GetLastError() == "A game mode is already running");
}

TEST_CASE("A map validates its modes and owns its checkpoint and restart scene", "[sandbox][gameplay][maps]") {
    GameplayTests::World world;
    auto& map = world.scene.CreateEntity("Map").AddComponent<Gameplay::MapInfo>();
    world.scene.SetSourceAsset({91, 92});
    CHECK(map.GetRestartScene() == Spot::AssetId{91, 92});
    map.restartScene = {93, 94};
    CHECK(map.GetRestartScene() == map.restartScene);
    CHECK(map.checkpoint == "world.json");
    map.modes = {ModeId};
    CHECK(map.SupportsMode(ModeId));
    CHECK_FALSE(map.SupportsMode({81, 82}));
    CHECK_FALSE(map.StartMode(MakeMode({81, 82})));
    CHECK_FALSE(Gameplay::GameMode::Get(world.scene));
    CHECK_FALSE(map.StartMode({}));
    CHECK_FALSE(map.StartMode(MakeMode(ModeId, "Not Networked", false)));
    CHECK_FALSE(map.StartMode(MakeMode(ModeId, "Inactive", true, false)));
    CHECK_FALSE(Gameplay::GameMode::Get(world.scene));
    REQUIRE(map.StartMode(MakeMode()));
}

TEST_CASE("Clients receive the server's selected mode instead of starting their map's default",
          "[sandbox][gameplay][maps][network]") {
    RestoreTime restore;
    Spot::LoopbackNetwork network;
    GameplayTests::World server;
    GameplayTests::World client;
    const auto chosen = MakeMode();
    server.scene.CreateEntity("Map").AddComponent<Gameplay::MapInfo>().defaultMode = chosen;
    auto& clientMap = client.scene.CreateEntity("Map").AddComponent<Gameplay::MapInfo>();
    clientMap.defaultMode = MakeMode({81, 82}, "Other Mode");
    auto listen = MakeHost(network);
    REQUIRE(listen->Listen(9050, 4));
    auto serverSession = std::make_shared<Spot::NetworkSession>(std::move(listen));
    server.scene.AddSystem<Spot::NetworkSystem>(serverSession);
    auto join = MakeHost(network);
    REQUIRE(join->Connect({"localhost", 9050}) != Spot::NetPeerId::Invalid);
    auto clientSession = std::make_shared<Spot::NetworkSession>(std::move(join));
    Spot::NetworkSystemConfig config;
    config.loadPrefab = [chosen](Spot::AssetId id) { return id == ModeId ? chosen : Spot::AssetRef<Spot::Prefab>(); };
    client.scene.AddSystem<Spot::NetworkSystem>(clientSession, config);
    CHECK_FALSE(clientMap.StartMode(clientMap.defaultMode));
    TestMode::s_starts = 0;
    for (int i = 0; i < 16; ++i) {
        server.Step();
        client.Step();
    }
    REQUIRE(Gameplay::GameMode::Get(client.scene));
    CHECK(Gameplay::GameMode::Get(client.scene)->name == "Test Mode");
    CHECK_FALSE(client.scene.FindEntity("Other Mode"));
    CHECK(TestMode::s_starts == 2);
}

TEST_CASE("Map and mode catalogs discover custom imported content without starting it", "[sandbox][gameplay][maps]") {
    const auto mode = MakeMode();
    TempDirectory directory;
    const auto sources = directory.GetPath() / "assets";
    const auto imported = directory.GetPath() / "imported";
    std::filesystem::create_directories(sources);
    std::ofstream(sources / "custom.prefab") << mode->GetEntityText();
    Spot::AssetDatabase database(sources, imported);
    REQUIRE(database.Refresh() == 1);
    const auto id = database.FindByPath("custom.prefab");
    REQUIRE(id);
    Spot::Tests::RegisteredAssetManager assets(imported);
    Spot::Scene scene;
    auto& map = scene.CreateEntity("Map").AddComponent<Gameplay::MapInfo>();
    map.name = "A Custom Map";
    map.modes = {*id};
    map.defaultMode = assets.Load<Spot::Prefab>(*id);
    std::ofstream(sources / "custom.scene") << scene.SaveToString();
    std::ofstream(sources / "not_a_map.scene") << Spot::Scene().SaveToString();
    REQUIRE(database.Refresh() == 2);
    assets.RefreshManifest();
    TestMode::s_starts = 0;
    Gameplay::MapCatalog maps;
    Gameplay::ModeCatalog modes;
    maps.Refresh(assets);
    modes.Refresh(assets);
    REQUIRE(maps.GetEntries().size() == 1);
    REQUIRE(modes.GetEntries().size() == 1);
    const auto sceneId = database.FindByPath("custom.scene");
    REQUIRE(sceneId);
    REQUIRE(maps.Find(*sceneId));
    CHECK(maps.Find(*sceneId)->name == map.name);
    CHECK(maps.Find(*sceneId)->defaultMode == *id);
    CHECK(maps.Find(*sceneId)->SupportsMode(*id));
    CHECK_FALSE(maps.Find(*sceneId)->SupportsMode({1, 2}));
    REQUIRE(modes.Find(*id));
    CHECK(modes.Find(*id)->name == "Test Mode");
    CHECK(TestMode::s_starts == 0);
}

TEST_CASE("Modes can disallow pausing even when playing alone", "[sandbox][gameplay][maps][session]") {
    RestoreTime restore;
    Spot::Time::SetTimeScale(1.0f);
    GameplayTests::World world;
    world.scene.CreateEntity("Rules").AddComponent<Gameplay::GameMode>().allowsTimeScale = false;
    auto& menu = world.scene.CreateEntity("Menu").AddComponent<Gameplay::Menu>();
    menu.pausesGame = true;
    world.Step();
    CHECK_FALSE(Gameplay::WorldTime::IsScalingAllowed(world.scene));
    menu.Open();
    CHECK(Spot::Time::GetTimeScale() == 1.0f);
    menu.Close();
}

TEST_CASE("A private host may pause and resumes when another player connects",
          "[sandbox][gameplay][maps][session][network]") {
    RestoreTime restore;
    Spot::Time::SetTimeScale(1.0f);
    Spot::LoopbackNetwork network;
    GameplayTests::World server;
    GameplayTests::World client;
    auto& menu = server.scene.CreateEntity("Menu").AddComponent<Gameplay::Menu>();
    menu.pausesGame = true;
    auto host = MakeHost(network);
    REQUIRE(host->Listen(9051, 4));
    auto session = std::make_shared<Spot::NetworkSession>(std::move(host));
    server.scene.AddSystem<Spot::NetworkSystem>(session);
    server.Step();
    CHECK(Gameplay::WorldTime::IsScalingAllowed(server.scene));
    menu.Open();
    CHECK(Spot::Time::GetTimeScale() == 0.0f);
    auto join = MakeHost(network);
    REQUIRE(join->Connect({"localhost", 9051}) != Spot::NetPeerId::Invalid);
    auto clientSession = std::make_shared<Spot::NetworkSession>(std::move(join));
    client.scene.AddSystem<Spot::NetworkSystem>(clientSession);
    for (int i = 0; i < 12; ++i) {
        server.scene.Update(0.0f);
        client.scene.Update(0.0f);
    }
    CHECK(Spot::Time::GetTimeScale() == 1.0f);
    CHECK_FALSE(Gameplay::WorldTime::IsScalingAllowed(server.scene));
    CHECK_FALSE(Gameplay::WorldTime::IsScalingAllowed(client.scene));
    menu.Close();
    CHECK(Spot::Time::GetTimeScale() == 1.0f);
}
