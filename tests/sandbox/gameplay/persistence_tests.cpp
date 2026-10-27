#include "gameplay_world.h"
#include "temp_directory.h"

#include "gameplay/persistence/world_save.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <string>
#include <vector>

namespace {

// Lets go of what it holds before a load and finds what was loaded, as a carrier or a spawner would.
class Keeper final : public Spot::Component, public Gameplay::SaveListener {
public:
    int before = 0;
    std::vector<std::string> loaded;

    void OnBeforeLoad() override { ++before; }
    void OnAfterLoad(std::span<Spot::Entity* const> roots) override {
        for (const Spot::Entity* root : roots) {
            loaded.push_back(root->GetName());
        }
    }
};

} // namespace

TEST_CASE("World saves keep the persistent roots and nothing else", "[sandbox][gameplay]") {
    GameplayTests::World world;
    TempDirectory directory;
    const auto path = directory.GetPath() / "world.json";
    auto& keeper = world.scene.CreateEntity("Keeper").AddComponent<Keeper>();
    auto& crate = world.scene.CreateEntity("Crate");
    crate.AddComponent<Gameplay::Persistent>();
    crate.GetTransform().SetPosition({1.0f, 2.0f, 3.0f});
    crate.CreateChild("Lid");
    auto& rocket = world.scene.CreateEntity("Rocket");
    rocket.AddComponent<Gameplay::Persistent>();
    rocket.AddComponent<Gameplay::Transient>();
    world.scene.CreateEntity("Level");
    world.Step();
    CHECK(Gameplay::WorldSave::IsSaved(crate));
    CHECK_FALSE(Gameplay::WorldSave::IsSaved(rocket));
    CHECK_FALSE(Gameplay::WorldSave::IsSaved(world.ground));

    REQUIRE(Gameplay::WorldSave::Save(world.scene, path));
    crate.GetTransform().SetPosition({9.0f, 9.0f, 9.0f});
    REQUIRE(Gameplay::WorldSave::Load(world.scene, path));
    world.Step();
    CHECK(keeper.before == 1);
    CHECK(keeper.loaded == std::vector<std::string>{"Crate"});
    Spot::Entity* restored = world.scene.FindEntity("Crate");
    REQUIRE(restored);
    CHECK(restored->GetTransform().GetPosition() == Spot::Vector3{1.0f, 2.0f, 3.0f});
    CHECK(restored->FindChild("Lid"));
    // What is not persistent stays as it was: the rocket and the level are still there, once.
    CHECK(world.scene.FindEntity("Rocket"));
    CHECK(world.scene.FindEntity("Level"));
    CHECK(world.scene.GetRootEntities().size() == 5);
}
