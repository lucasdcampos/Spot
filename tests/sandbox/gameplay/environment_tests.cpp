#include "gameplay_world.h"

#include "gameplay/core/trace_volume.h"
#include "gameplay/effects/effect_library.h"
#include "gameplay/effects/effect_pool.h"
#include "gameplay/environment/day_cycle.h"
#include "gameplay/environment/night_light.h"
#include "gameplay/environment/water.h"
#include "gameplay/network/session.h"

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using Catch::Approx;

namespace {

std::unique_ptr<Spot::NetHost> MakeHost(Spot::LoopbackNetwork& network) {
    return std::make_unique<Spot::NetHost>(network.CreateTransport(),
                                           Spot::NetHostConfig{.channels = Spot::NetworkSession::ChannelCount});
}

} // namespace

TEST_CASE("Water ripples stay on the surface and reject inactive water and invalid strengths",
          "[sandbox][gameplay][water]") {
    GameplayTests::World world;
    auto& pond = world.scene.CreateEntity("Pond");
    pond.GetTransform().SetPosition({0.0f, 1.0f, 0.0f});
    pond.GetTransform().SetScale({10.0f, 1.0f, 10.0f});
    auto& water = pond.AddComponent<Gameplay::Water>();
    Gameplay::ParticleLayer ring;
    ring.settings.startColor.a = 0.8f;
    world.scene.CreateEntity("Effects").AddComponent<Gameplay::EffectLibrary>().effects = {
        {.name = "splash.ripples", .layers = {ring}}};
    world.Step();
    water.Ripple({0.0f, 50.0f, 0.0f}, 0.25f, 3.0f);
    auto* emitter = world.scene.FindComponent<Spot::ParticleSystem>();
    REQUIRE(emitter);
    REQUIRE(emitter->GetParticles().size() == 1);
    CHECK(emitter->GetSettings().startColor.a == Approx(0.2f));
    CHECK(emitter->GetEntity().GetTransform().GetPosition().y == Approx(1.025f));
    CHECK(emitter->GetEntity().GetTransform().GetScale().x == Approx(3.0f));
    water.Ripple({30.0f, 1.0f, 0.0f}, 1.0f);
    water.Ripple({}, 0.0f);
    water.Ripple({}, -1.0f);
    water.Ripple({}, std::numeric_limits<float>::quiet_NaN());
    water.Ripple({}, 1.0f, std::numeric_limits<float>::infinity());
    water.SetEnabled(false);
    water.Ripple({}, 1.0f);
    CHECK(Gameplay::EffectPool::Get(world.scene).GetEmitterCount() == 1);
}

TEST_CASE("Water is a volume traces meet at its surface and blasts at or under it", "[sandbox][gameplay]") {
    GameplayTests::World world;
    auto& pond = world.scene.CreateEntity("Pond");
    pond.GetTransform().SetPosition({0.0f, 1.0f, 0.0f});
    pond.GetTransform().SetScale({10.0f, 1.0f, 10.0f});
    auto& water = pond.AddComponent<Gameplay::Water>();
    world.Step();

    const auto hit = Gameplay::Traces::Raycast(world.scene, {0.0f, 6.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f);
    REQUIRE(hit);
    CHECK(hit->volume == &water);
    CHECK(hit->distance == Approx(5.0f));
    // Not from under it, beside it, or short of it.
    CHECK_FALSE(Gameplay::Traces::Raycast(world.scene, {0.0f, 0.5f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f));
    CHECK_FALSE(Gameplay::Traces::Raycast(world.scene, {20.0f, 6.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f));
    CHECK_FALSE(Gameplay::Traces::Raycast(world.scene, {0.0f, 6.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 2.0f));
    CHECK(water.GetShotDepth() == water.shotDepth);

    CHECK(Gameplay::Traces::Blast(world.scene, {0.0f, 20.0f, 0.0f}, 3.0f) == Gameplay::BlastMedium::None);
    CHECK(Gameplay::Traces::Blast(world.scene, {0.0f, 1.2f, 0.0f}, 3.0f) == Gameplay::BlastMedium::Surface);
    CHECK(Gameplay::Traces::Blast(world.scene, {0.0f, 0.0f, 0.0f}, 3.0f) == Gameplay::BlastMedium::Inside);
}

TEST_CASE("The day cycle lights effects and street lamps as the day goes", "[sandbox][gameplay]") {
    GameplayTests::World world;
    auto& sun = world.scene.CreateEntity("Sun");
    sun.AddComponent<Spot::DirectionalLight>();
    auto& cycle = sun.AddComponent<Gameplay::DayCycle>();
    cycle.dayLength = 0.0f;
    auto& lamp = world.scene.CreateEntity("Lamp");
    lamp.AddComponent<Spot::PointLight>();
    lamp.AddComponent<Gameplay::NightLight>();
    world.Step();

    cycle.SetTimeOfDay(0.5f);
    world.Step();
    CHECK(cycle.GetDaylight() == Approx(1.0f));
    CHECK(Gameplay::EffectPool::Get(world.scene).GetLight() == Approx(1.0f));
    CHECK_FALSE(lamp.GetComponent<Spot::PointLight>()->IsEnabled());

    cycle.SetTimeOfDay(0.0f);
    world.Step();
    CHECK(cycle.GetDaylight() == Approx(0.0f));
    CHECK(Gameplay::EffectPool::Get(world.scene).GetLight() == Approx(0.12f));
    CHECK(lamp.GetComponent<Spot::PointLight>()->IsEnabled());
}

TEST_CASE("Day cycle time of day can be changed on servers by operators and replicates to clients",
          "[sandbox][gameplay][environment][network]") {
    Spot::LoopbackNetwork network;
    GameplayTests::World server;
    GameplayTests::World client;

    const Spot::AssetId SceneAsset{0x12, 0x34};
    Spot::Scene authoring;
    authoring.CreateEntity("Ground");
    auto& authoringSun = authoring.CreateEntity("Sun");
    authoringSun.AddComponent<Spot::DirectionalLight>();
    auto& authoringCycle = authoringSun.AddComponent<Gameplay::DayCycle>();
    authoringCycle.dayLength = 0.0f;
    authoringSun.AddComponent<Spot::NetworkObject>();
    const std::string level = authoring.SaveToString();

    REQUIRE(server.scene.LoadFromString(level));
    server.scene.SetSourceAsset(SceneAsset);
    REQUIRE(client.scene.LoadFromString(level));
    client.scene.SetSourceAsset(SceneAsset);

    auto* serverSun = server.scene.FindEntity("Sun");
    auto* clientSun = client.scene.FindEntity("Sun");
    REQUIRE(serverSun);
    REQUIRE(clientSun);
    auto* serverCycle = serverSun->GetComponent<Gameplay::DayCycle>();
    auto* clientCycle = clientSun->GetComponent<Gameplay::DayCycle>();
    REQUIRE(serverCycle);
    REQUIRE(clientCycle);

    serverCycle->SetTimeOfDay(0.5f);

    auto listen = MakeHost(network);
    REQUIRE(listen->Listen(9100, 4));
    auto serverSession = std::make_shared<Spot::NetworkSession>(std::move(listen));
    server.scene.AddSystem<Spot::NetworkSystem>(serverSession);

    auto join = MakeHost(network);
    REQUIRE(join->Connect({"localhost", 9100}) != Spot::NetPeerId::Invalid);
    auto clientSession = std::make_shared<Spot::NetworkSession>(std::move(join));
    client.scene.AddSystem<Spot::NetworkSystem>(clientSession);

    Gameplay::Session serverRoom;
    serverRoom.Bind(*serverSession);
    Gameplay::Session clientRoom;
    clientRoom.Bind(*clientSession);

    for (int i = 0; i < 20; ++i) {
        server.Step();
        client.Step();
    }

    const auto peers = serverSession->GetHost().GetPeers();
    REQUIRE(peers.size() == 1);
    const Spot::NetPeerId guestPeer = peers.front();

    CHECK_FALSE(clientRoom.IsOperator());
    CHECK(serverCycle->timeOfDay == Catch::Approx(0.5f));
    CHECK(clientCycle->timeOfDay == Catch::Approx(0.5f));

    // Non-operator client cannot alter time of day
    clientCycle->SetTimeOfDay(0.30f);
    for (int i = 0; i < 10; ++i) {
        server.Step();
        client.Step();
    }
    CHECK(serverCycle->timeOfDay == Catch::Approx(0.5f));
    CHECK(clientCycle->timeOfDay == Catch::Approx(0.5f));

    // Grant operator permission to the guest
    serverRoom.SetOperator(guestPeer, true);
    for (int i = 0; i < 10; ++i) {
        server.Step();
        client.Step();
    }
    CHECK(clientRoom.IsOperator());

    // Operator client alters time of day to morning (0.30)
    clientCycle->SetTimeOfDay(0.30f);
    for (int i = 0; i < 10; ++i) {
        server.Step();
        client.Step();
    }
    CHECK(serverCycle->timeOfDay == Catch::Approx(0.30f));
    CHECK(clientCycle->timeOfDay == Catch::Approx(0.30f));

    // Operator client alters time of day to night (0.90)
    clientCycle->SetTimeOfDay(0.90f);
    for (int i = 0; i < 10; ++i) {
        server.Step();
        client.Step();
    }
    CHECK(serverCycle->timeOfDay == Catch::Approx(0.90f));
    CHECK(clientCycle->timeOfDay == Catch::Approx(0.90f));
    CHECK(clientCycle->GetDaylight() < 0.05f);
}
