#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

namespace {

class Health : public Spot::Component {
public:
    int value = 100;
};

class Shield final : public Health {};

class Movement final : public Spot::Component {
    SPOT_REQUIRE_COMPONENTS(Health)
};

class Player final : public Spot::Component {
    SPOT_REQUIRE_COMPONENTS(Movement)

public:
    int lives = 3;
};

} // namespace

TEST_CASE("Adding a component adds the components it requires first, and theirs", "[scene][requirements]") {
    Spot::Scene scene;
    Spot::Entity& entity = scene.CreateEntity();
    Player& player = entity.AddComponent<Player>();
    CHECK(player.lives == 3);
    REQUIRE(entity.HasComponent<Movement>());
    REQUIRE(entity.HasComponent<Health>());
    CHECK(entity.GetComponentIndex(*entity.GetComponent<Health>()) == 0);
    CHECK(entity.GetComponentIndex(*entity.GetComponent<Movement>()) == 1);
    CHECK(entity.GetComponentIndex(player) == 2);

    // What the entity already has, or a type derived from it, is kept rather than added again.
    Spot::Entity& shielded = scene.CreateEntity();
    shielded.AddComponent<Shield>();
    shielded.AddComponent<Movement>();
    CHECK(shielded.GetComponents<Health>().size() == 1);
}

TEST_CASE("Components made from their registered name add what they require", "[scene][requirements]") {
    Spot::RegisterComponent<Health>("RequirementHealth");
    Spot::RegisterComponent<Movement>("RequirementMovement");
    Spot::Scene scene;
    Spot::Entity& entity = scene.CreateEntity();
    entity.AddComponent(Spot::CreateComponent("RequirementMovement"));
    CHECK(entity.HasComponent<Health>());
    Spot::UnregisterComponent("RequirementMovement");
    Spot::UnregisterComponent("RequirementHealth");
}

TEST_CASE("A required component is kept while what requires it is there", "[scene][requirements]") {
    const auto sink = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks scoped({sink});
    Spot::Scene scene;
    Spot::Entity& entity = scene.CreateEntity("Hero");
    entity.AddComponent<Movement>();
    Health& health = *entity.GetComponent<Health>();

    entity.RemoveComponent(health);
    CHECK(entity.HasComponent<Health>());
    REQUIRE(sink->GetRecords().size() == 1);
    CHECK(sink->GetRecords()[0].message.find("is required by") != std::string::npos);

    // Another component of the type stands in for it, and once the dependent is gone it can go too.
    auto& spare = entity.AddComponent<Health>();
    entity.RemoveComponent(health);
    scene.Update(0.0f);
    CHECK(entity.GetComponent<Health>() == &spare);
    entity.RemoveComponent<Movement>();
    entity.RemoveComponent(spare);
    scene.Update(0.0f);
    CHECK_FALSE(entity.HasComponent<Health>());
}

TEST_CASE("Loading a saved scene adds nothing beyond what was saved", "[scene][requirements]") {
    Spot::RegisterComponent<Health>("RequirementHealth");
    Spot::RegisterComponent<Movement>("RequirementMovement");
    Spot::Scene saved;
    saved.CreateEntity().AddComponent<Movement>();
    const std::string text = saved.SaveToString();

    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(text));
    const Spot::Entity& entity = *loaded.GetRootEntities().front();
    CHECK(entity.GetComponents<Health>().size() == 1);
    CHECK(entity.GetComponents<Movement>().size() == 1);
    Spot::UnregisterComponent("RequirementMovement");
    Spot::UnregisterComponent("RequirementHealth");
}
