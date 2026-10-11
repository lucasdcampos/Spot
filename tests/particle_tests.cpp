#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <limits>
#include <spot.h>

using Catch::Approx;
namespace {
Spot::ParticleSettings Settings() {
    Spot::ParticleSettings settings;
    settings.emissionRate = 0;
    settings.gravity = {};
    settings.speed = {2, 2};
    settings.lifetime = {10, 10};
    return settings;
}
} // namespace
TEST_CASE("Particle emission is bounded and reuses expired storage", "[particles]") {
    auto settings = Settings();
    settings.maxParticles = 3;
    settings.lifetime = {0.1f, 0.1f};
    Spot::ParticleSimulation simulation(settings);
    simulation.Emit(7);
    CHECK(simulation.GetParticles().size() == 3);
    CHECK(simulation.GetDroppedCount() == 4);
    simulation.Stop();
    simulation.Advance(0.2f);
    CHECK_FALSE(simulation.IsAlive());
    simulation.Emit(2);
    CHECK(simulation.GetParticles().size() == 2);
}
TEST_CASE("Particle rate bursts and loops use simulation time", "[particles]") {
    auto settings = Settings();
    settings.duration = 0.5f;
    settings.loop = false;
    settings.emissionRate = 10;
    settings.bursts = {{0, 2}, {0.25f, 3}, {0.5f, 4}};
    Spot::ParticleSimulation simulation(settings);
    simulation.Advance(0.5f);
    CHECK(simulation.GetParticles().size() == 14);
    CHECK_FALSE(simulation.IsEmitting());
    simulation.Advance(0.5f);
    CHECK(simulation.GetParticles().size() == 14);
    settings.loop = true;
    settings.emissionRate = 0;
    settings.bursts = {{0, 2}};
    simulation.SetSettings(settings);
    simulation.Advance(1);
    CHECK(simulation.GetParticles().size() == 6);
}
TEST_CASE("Particle replay is deterministic across update partitions", "[particles]") {
    auto settings = Settings();
    settings.shape = Spot::ParticleShape::Sphere;
    settings.emissionRate = 20;
    settings.gravity = {0, -2, 0};
    settings.sizeOverLifetime = Spot::Curve::Linear(1, 0);
    Spot::ParticleSimulation a(settings), b(settings);
    a.Advance(1);
    for (int i = 0; i < 60; ++i)
        b.Advance(1.0f / 60);
    REQUIRE(a.GetParticles().size() == b.GetParticles().size());
    for (std::size_t i = 0; i < a.GetParticles().size(); ++i) {
        CHECK(a.GetParticles()[i].position == b.GetParticles()[i].position);
        CHECK(a.GetParticles()[i].size == b.GetParticles()[i].size);
    }
    b.Seek(1);
    CHECK(a.GetParticles()[0].position == b.GetParticles()[0].position);
    b.Advance(std::numeric_limits<float>::infinity());
    CHECK(b.GetTime() == Approx(1));
}
TEST_CASE("Particle gravity lifetime modules and spaces compose", "[particles]") {
    auto settings = Settings();
    settings.gravity = {0, -2, 0};
    settings.size = {2, 2};
    settings.sizeOverLifetime = Spot::Curve::Linear(1, 0);
    settings.colorOverLifetime = Spot::Gradient({{0, Spot::Color::White}, {1, Spot::Color::Transparent}});
    Spot::ParticleSimulation world(settings);
    world.Emit(1, Spot::Matrix4::Translation({3, 0, 0}));
    world.Advance(1);
    const auto& p = world.GetParticles()[0];
    CHECK(p.position.x == 3);
    CHECK(p.position.y == Approx(1).margin(1e-4));
    CHECK(p.velocity.y == Approx(0).margin(1e-4));
    CHECK(p.size == Approx(1.8f));
    CHECK(p.color.a == Approx(0.9f));
    settings.space = Spot::ParticleSpace::Local;
    Spot::ParticleSimulation local(settings);
    local.Emit(1, Spot::Matrix4::Translation({3, 0, 0}));
    CHECK(local.GetParticles()[0].position.x == 0);
}
TEST_CASE("Particle shapes sample inside their bounds", "[particles]") {
    auto settings = Settings();
    settings.radius = 2;
    settings.boxSize = {2, 4, 6};
    for (auto shape : {Spot::ParticleShape::Sphere, Spot::ParticleShape::Cone, Spot::ParticleShape::Box}) {
        settings.shape = shape;
        Spot::ParticleSimulation simulation(settings);
        simulation.Emit(100);
        for (const auto& p : simulation.GetParticles()) {
            if (shape == Spot::ParticleShape::Sphere)
                CHECK(Spot::Length(p.position) <= 2.001f);
            if (shape == Spot::ParticleShape::Cone) {
                CHECK(Spot::Length(p.position) <= 2.001f);
                CHECK(Spot::Normalize(p.velocity).y >= std::cos(settings.coneAngle));
            }
            if (shape == Spot::ParticleShape::Box) {
                CHECK(std::abs(p.position.x) <= 1);
                CHECK(std::abs(p.position.y) <= 2);
                CHECK(std::abs(p.position.z) <= 3);
            }
        }
    }
}
TEST_CASE("Particle components simulate lazily and survive scene round trips", "[particles][scene]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    auto& effect = entity.AddComponent<Spot::ParticleSystem>();
    auto settings = Settings();
    settings.bursts = {{0, 4}};
    effect.SetSettings(settings);
    CHECK_FALSE(scene.GetSystem<Spot::ParticleSceneSystem>());
    scene.Update(0.1f);
    REQUIRE(scene.GetSystem<Spot::ParticleSceneSystem>());
    CHECK(effect.GetParticles().size() == 4);
    const auto time = effect.GetSimulation().GetTime();
    effect.SetEnabled(false);
    scene.Update(1);
    CHECK(effect.GetSimulation().GetTime() == time);
    Spot::Scene copy;
    REQUIRE(copy.LoadFromString(scene.SaveToString()));
    auto* loaded = copy.FindComponent<Spot::ParticleSystem>();
    REQUIRE(loaded);
    CHECK(loaded->GetSettings() == settings);
    CHECK(loaded->GetParticles().empty());
    effect.SetEnabled(true);
    scene.Update(0.1f);
    CHECK(effect.GetSimulation().GetTime() > time);
    entity.RemoveComponent(effect);
    scene.Update(0.1f);
    CHECK_FALSE(scene.FindComponent<Spot::ParticleSystem>());
}
TEST_CASE("Particle settings contain malformed serialized values", "[particles]") {
    auto settings = Settings();
    settings.maxParticles = 0;
    settings.lifetime = {-1, -2};
    settings.emissionRate = std::numeric_limits<float>::quiet_NaN();
    settings.duration = 0;
    settings.shape = static_cast<Spot::ParticleShape>(100);
    settings.flipbookRows = 0;
    settings.Validate();
    CHECK(settings.maxParticles == 1);
    CHECK(settings.lifetime.min > 0);
    CHECK(settings.lifetime.max >= settings.lifetime.min);
    CHECK(settings.emissionRate == 0);
    CHECK(settings.shape == Spot::ParticleShape::Point);
    CHECK(settings.flipbookRows == 1);
}

TEST_CASE("Particle batches sort across emitters and preserve alpha order", "[particles][rendering]") {
    auto settings = Settings();
    Spot::Particle near, far;
    near.position = {0, 0, -2};
    far.position = {0, 0, -5};
    near.size = far.size = 1;
    near.color = Spot::Color::Red;
    far.color = Spot::Color::Blue;
    Spot::ParticleBatch batch;
    batch.Add(std::span(&near, 1), settings, {});
    batch.Add(std::span(&far, 1), settings, {});
    settings.blend = Spot::ParticleBlend::Additive;
    batch.Add(std::span(&far, 1), settings, {});
    batch.Sort();
    REQUIRE(batch.GetDraws().size() == 3);
    CHECK(batch.GetDraws()[0].instance.color == Spot::Color::Blue);
    CHECK(batch.GetDraws()[1].instance.color == Spot::Color::Red);
    CHECK(batch.GetDraws()[2].blend == Spot::ParticleBlend::Additive);
}
TEST_CASE("Particle billboards stretch rotate and address flipbooks", "[particles][rendering]") {
    auto settings = Settings();
    settings.space = Spot::ParticleSpace::Local;
    settings.flipbookColumns = 2;
    settings.flipbookRows = 2;
    Spot::Particle p;
    p.position = {0, 0, -4};
    p.velocity = {4, 0, 0};
    p.size = 1;
    p.age = 0.75f;
    p.lifetime = 1;
    p.color = Spot::Color::White;
    Spot::ParticleBatch batch;
    const auto emitter = Spot::Matrix4::Translation({2, 0, 0});
    batch.Add(std::span(&p, 1), settings, {}, emitter);
    REQUIRE(batch.GetDraws().size() == 1);
    CHECK(batch.GetDraws()[0].instance.centerAndSize.x == 2);
    CHECK(batch.GetDraws()[0].instance.uvRect == Spot::Vector4{0.5f, 0.5f, 1, 1});
    batch.Clear();
    settings.facing = Spot::ParticleFacing::Stretched;
    batch.Add(std::span(&p, 1), settings, {}, emitter);
    CHECK(batch.GetDraws()[0].instance.up.x > 1);
    CHECK(batch.GetDraws()[0].instance.rightAndSoftDistance.y == Approx(-1));
    batch.Clear();
    settings.facing = Spot::ParticleFacing::Billboard;
    p.rotation = Spot::ToRadians(90);
    batch.Add(std::span(&p, 1), settings, {}, emitter);
    CHECK(batch.GetDraws()[0].instance.rightAndSoftDistance.y == Approx(1));
}

TEST_CASE("Reconfiguring pooled particles retains contiguous storage and does not restart inspection", "[particles]") {
    auto settings = Settings();
    settings.maxParticles = 8;
    Spot::ParticleSimulation simulation(settings);
    simulation.Emit(1);
    const auto* storage = simulation.GetParticles().data();
    settings.seed = 42;
    simulation.SetSettings(settings);
    simulation.Emit(1);
    CHECK(simulation.GetParticles().data() == storage);
    Spot::Scene scene;
    auto& effect = scene.CreateEntity().AddComponent<Spot::ParticleSystem>();
    effect.SetSettings(settings);
    scene.Update(0.2f);
    const auto time = effect.GetSimulation().GetTime();
    const auto saved = Spot::SaveComponentToString(effect);
    REQUIRE(Spot::LoadComponentFromString(effect, saved));
    CHECK(effect.GetSimulation().GetTime() == time);
    Spot::FrameClock clock;
    clock.Start({});
    REQUIRE(clock.SetTimeScale(2));
    clock.Tick(Spot::FrameClock::TimePoint{} + std::chrono::milliseconds(100));
    scene.Update(clock.GetDeltaTime());
    CHECK(effect.GetSimulation().GetTime() == Approx(time + 0.2));
}
