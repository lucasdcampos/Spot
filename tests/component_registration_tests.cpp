#include <spot.h>
#include <spot/scene/scripting/scripting_components.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("System components register explicitly without an application", "[scene][registration]") {
    REQUIRE(Spot::GetComponentTypeNames().empty());
    REQUIRE_FALSE(Spot::CreateComponent("Rigidbody"));

    Spot::RegisterAnimationComponents();
    Spot::RegisterAudioComponents();
    Spot::RegisterNetworkComponents();
    Spot::RegisterParticleComponents();
    Spot::RegisterPhysicsComponents();
    Spot::RegisterPhysics2DComponents();
    Spot::RegisterRenderComponents();
    Spot::RegisterUiComponents();
    Spot::RegisterScriptComponents();

    const auto names = Spot::GetComponentTypeNames();
    REQUIRE(names.size() == 57);
    for (const std::string& name : names) {
        const auto component = Spot::CreateComponent(name);
        REQUIRE(component);
        CHECK(Spot::GetComponentTypeName(*component) == name);
    }

    // Registered again, as by a module of another application, a type keeps what scripts may do with it.
    const std::size_t members = Spot::GetComponentScriptMembers("AudioSource").size();
    CHECK(members > 0);
    Spot::RegisterAudioComponents();
    CHECK(Spot::GetComponentScriptMembers("AudioSource").size() == members);

    Spot::RegisterPhysicsComponents();
    CHECK(Spot::GetComponentTypeNames() == names);
    Spot::UnregisterComponent("Rigidbody");
    CHECK_FALSE(Spot::CreateComponent("Rigidbody"));
    Spot::RegisterPhysicsComponents();
    CHECK(Spot::CreateComponent("Rigidbody"));
    CHECK(Spot::GetComponentTypeNames() == names);
}
