#include <spot.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Render worlds keep objects side by side under ids that stay safe", "[render_world]") {
    Spot::RenderWorld world;
    const auto a = world.AddMesh({.layer = 1});
    const auto b = world.AddMesh({.layer = 2});
    const auto c = world.AddMesh({.layer = 3});
    REQUIRE(world.GetMeshes().size() == 3);

    world.Remove(a);
    CHECK_FALSE(world.Contains(a));
    CHECK(world.GetMesh(a) == nullptr);
    REQUIRE(world.GetMeshes().size() == 2);
    CHECK(world.GetMesh(b)->desc.layer == 2);
    CHECK(world.GetMesh(c)->desc.layer == 3);

    // The freed slot is reused under a new id, which the old one does not reach.
    const auto d = world.AddMesh({.layer = 4});
    CHECK(d != a);
    CHECK_FALSE(world.Contains(a));
    CHECK_FALSE(world.SetTransform(a, Spot::Matrix4::Translation({1, 0, 0})));
    CHECK(world.GetMesh(d)->desc.layer == 4);

    CHECK(world.SetTransform(c, Spot::Matrix4::Scale({-1, 1, 1})));
    CHECK(world.GetMesh(c)->desc.transform == Spot::Matrix4::Scale({-1, 1, 1}));
    for (std::size_t i = 0; i < world.GetMeshes().size(); ++i) {
        CHECK(world.GetMesh(world.GetMeshIds()[i]) == &world.GetMeshes()[i]);
    }

    world.Clear();
    CHECK(world.GetMeshes().empty());
    CHECK_FALSE(world.Contains(b));
}

TEST_CASE("The scene's render world follows its mesh renderers", "[render_world]") {
    Spot::Scene scene;
    auto& system = scene.AddSystem<Spot::RenderWorldSystem>();
    Spot::Entity& crate = scene.CreateEntity("Crate");
    crate.AddComponent<Spot::MeshRenderer>();
    Spot::Entity& barrel = scene.CreateEntity("Barrel");
    auto& barrelRenderer = barrel.AddComponent<Spot::MeshRenderer>();
    scene.Update(0.0f);

    REQUIRE(system.Sync().GetMeshes().size() == 2);

    SECTION("Moves reach the world, a parent's included") {
        Spot::Entity& lid = scene.CreateEntity("Lid");
        lid.SetParent(&crate);
        lid.AddComponent<Spot::MeshRenderer>();
        system.Sync();
        crate.GetTransform().SetPosition({0, 5, 0});
        bool found = false;
        for (const auto& object : system.Sync().GetMeshes()) {
            found = found || object.desc.transform == lid.GetTransform().GetWorldMatrix();
        }
        CHECK(found);
        CHECK(lid.GetTransform().GetWorldPosition().y == 5.0f);
    }

    SECTION("Changes to the renderer alone reach the world, through the scene's record of changes") {
        const auto version = scene.GetStructureVersion();
        barrelRenderer.SetCastingShadows(false);
        CHECK(scene.GetStructureVersion() == version);
        int changed = 0;
        for (const auto& object : system.Sync().GetMeshes()) {
            changed += !object.desc.castsShadows;
        }
        CHECK(changed == 1);
    }

    SECTION("Changes to the renderer and its entity reach the world") {
        barrelRenderer.SetCastingShadows(false);
        barrel.SetLayer(7);
        int changed = 0;
        for (const auto& object : system.Sync().GetMeshes()) {
            changed += !object.desc.castsShadows && object.desc.layer == 7;
        }
        CHECK(changed == 1);
    }

    SECTION("Renderers gone or disabled leave the world, and come back when enabled") {
        barrelRenderer.SetEnabled(false);
        CHECK(system.Sync().GetMeshes().size() == 1);
        barrelRenderer.SetEnabled(true);
        CHECK(system.Sync().GetMeshes().size() == 2);
        crate.Destroy();
        CHECK(system.Sync().GetMeshes().size() == 1);
        barrel.SetActive(false);
        CHECK(system.Sync().GetMeshes().empty());
    }
}
