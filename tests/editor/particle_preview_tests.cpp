#include "scene/particle_preview.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <spot.h>

TEST_CASE("Selected particle previews replay without running gameplay or changing authored data",
          "[editor][particles]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    auto& effect = entity.AddComponent<Spot::ParticleSystem>();
    Spot::ParticleSettings settings;
    settings.bursts = {{0, 3}};
    settings.emissionRate = 0;
    effect.SetSettings(settings);
    const auto authored = scene.SaveToString();
    Spot::Editor::ParticlePreview preview;
    preview.Select(scene, entity.GetId());
    preview.Update(scene, 0.5f);
    CHECK(effect.GetParticles().size() == 3);
    CHECK(preview.GetTime(scene) == Catch::Approx(0.5f));
    CHECK(scene.GetUpdateCount() == 0);
    CHECK(scene.SaveToString() == authored);
    const auto position = effect.GetParticles()[0].position;
    preview.SetPlaying(false);
    preview.Update(scene, 1);
    CHECK(effect.GetParticles()[0].position == position);
    preview.Seek(scene, 0.5f);
    CHECK(effect.GetParticles()[0].position == position);
    preview.Restart(scene);
    CHECK(preview.GetTime(scene) == 0);
    CHECK(effect.GetParticles().empty());
    preview.SetPlaying(true);
    preview.Update(scene, 0.1f);
    CHECK(effect.GetParticles().size() == 3);
}
TEST_CASE("Particle previews resolve stable component ids after selection changes and reloads", "[editor][particles]") {
    Spot::Scene scene;
    auto& first = scene.CreateEntity();
    auto& a = first.AddComponent<Spot::ParticleSystem>();
    auto& second = scene.CreateEntity();
    auto& b = second.AddComponent<Spot::ParticleSystem>();
    Spot::Editor::ParticlePreview preview;
    preview.Select(scene, first.GetId());
    preview.Update(scene, 0.5f);
    REQUIRE_FALSE(a.GetParticles().empty());
    preview.Select(scene, second.GetId());
    CHECK(a.GetParticles().empty());
    preview.Update(scene, 0.5f);
    CHECK_FALSE(b.GetParticles().empty());
    auto& other = second.AddComponent<Spot::ParticleSystem>();
    preview.Select(scene, second.GetId(), other.GetId());
    CHECK(b.GetParticles().empty());
    preview.Update(scene, 0.5f);
    CHECK_FALSE(other.GetParticles().empty());
    const auto id = second.GetId();
    second.RemoveComponent(other);
    preview.Update(scene, 1);
    CHECK(preview.GetEntityId() == Spot::EntityId::Invalid);
    preview.Select(scene, id);
    preview.Update(scene, 0.5f);
    preview.Select(scene, id, {}, 1);
    CHECK(preview.GetTime(scene) == 0);
    second.Destroy();
    preview.Update(scene, 1);
    CHECK(preview.GetTime(scene) == 0);
}
