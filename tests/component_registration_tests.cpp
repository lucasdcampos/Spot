#include <spot.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("System components register explicitly without an application", "[scene][registration]") {
    REQUIRE(Spot::GetComponentTypeNames().empty());
    REQUIRE_FALSE(Spot::CreateComponent("Rigidbody"));

    Spot::RegisterAnimationComponents();
    Spot::RegisterAudioComponents();
    Spot::RegisterParticleComponents();
    Spot::RegisterPhysicsComponents();
    Spot::RegisterPhysics2DComponents();
    Spot::RegisterRenderComponents();
    Spot::RegisterUiComponents();
    Spot::RegisterScriptComponents();

    const auto names = Spot::GetComponentTypeNames();
    REQUIRE(names.size() == 51);
    for (const std::string& name : names) {
        const auto component = Spot::CreateComponent(name);
        REQUIRE(component);
        CHECK(Spot::GetComponentTypeName(*component) == name);
    }

    Spot::RegisterPhysicsComponents();
    CHECK(Spot::GetComponentTypeNames() == names);
    Spot::UnregisterComponent("Rigidbody");
    CHECK_FALSE(Spot::CreateComponent("Rigidbody"));
    Spot::RegisterPhysicsComponents();
    CHECK(Spot::CreateComponent("Rigidbody"));
    CHECK(Spot::GetComponentTypeNames() == names);
}
