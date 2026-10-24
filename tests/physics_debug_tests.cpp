#include "spot/scene/physics/physics_world.h"
#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

using Catch::Approx;
using Spot::DebugLines;
using Spot::Entity;
using Spot::Scene;
using Spot::Vector3;

namespace {

bool HasColor(const DebugLines& lines, Spot::Color color) {
    return std::ranges::any_of(lines.GetLines(), [color](const Spot::DebugLine& line) { return line.color == color; });
}

} // namespace

TEST_CASE("Debug lines of their own leave DebugDraw's queue alone", "[physics][debug]") {
    Spot::DebugDraw::Clear();
    DebugLines lines;
    lines.Box({}, {1.0f, 1.0f, 1.0f});
    CHECK(lines.GetLines().size() == 12);
    CHECK(Spot::DebugDraw::GetLines().empty());
    lines.Update(0.0f);
    CHECK(lines.GetLines().empty());
}

TEST_CASE("Colliders, characters and joints draw from their components", "[physics][debug]") {
    Scene scene;
    scene.CreateEntity("Box").AddComponent<Spot::BoxCollider>();
    auto& zone = scene.CreateEntity("Zone");
    zone.AddComponent<Spot::SphereCollider>().SetTrigger(true);
    auto& prop = scene.CreateEntity("Prop");
    prop.AddComponent<Spot::CapsuleCollider>();
    prop.AddComponent<Spot::Rigidbody>();
    scene.CreateEntity("Mesh").AddComponent<Spot::MeshCollider>().SetMesh(
        std::make_shared<const Spot::MeshData>(Spot::MeshData::CreatePlane()));
    scene.CreateEntity("Player").AddComponent<Spot::CharacterController>();
    prop.AddComponent<Spot::FixedJoint>();

    DebugLines lines;
    Spot::DrawColliders(scene, lines);
    // A box, three circles, a capsule, a two-triangle plane, a character's capsule and a joint's cross and lines.
    const std::size_t capsule = 2 * 32 + 4 + 4 * 16;
    CHECK(lines.GetLines().size() == 12 + 3 * 32 + capsule + 6 + capsule + 3 + 1);
    // Static green, triggers yellow, dynamic orange and characters cyan.
    CHECK(HasColor(lines, {0.2f, 1.0f, 0.3f, 1.0f}));
    CHECK(HasColor(lines, {1.0f, 0.9f, 0.1f, 1.0f}));
    CHECK(HasColor(lines, {1.0f, 0.55f, 0.1f, 1.0f}));
    CHECK(HasColor(lines, {0.2f, 0.9f, 1.0f, 1.0f}));

    // Drawing does not make a scene's physics to tell whether its props sleep.
    Scene untouched;
    auto& crate = untouched.CreateEntity("Crate");
    crate.AddComponent<Spot::BoxCollider>();
    crate.AddComponent<Spot::Rigidbody>();
    Spot::DrawColliders(untouched, lines);
    CHECK(untouched.GetSystem<Spot::PhysicsWorld>() == nullptr);
}

TEST_CASE("The physics debug view draws colliders, contacts and queries before frames", "[physics][debug]") {
    Spot::DebugDraw::Clear();
    Scene scene;
    auto& ground = scene.CreateEntity("Ground");
    ground.GetTransform().SetPosition({0.0f, -0.5f, 0.0f});
    ground.AddComponent<Spot::BoxCollider>(Vector3{10.0f, 1.0f, 10.0f});
    auto& ball = scene.CreateEntity("Ball");
    ball.GetTransform().SetPosition({0.0f, 0.5f, 0.0f});
    ball.AddComponent<Spot::SphereCollider>();
    ball.AddComponent<Spot::Rigidbody>();
    for (int i = 0; i < 10; ++i) {
        scene.Update(Scene::DefaultFixedTimeStep);
    }

    scene.PrepareToRender();
    CHECK(Spot::DebugDraw::GetLines().empty());

    scene.GetOrAddSystem<Spot::PhysicsWorld>().SetDebugDraw(Spot::PhysicsDebugFlags::Contacts);
    scene.PrepareToRender();
    // A cross and an arrow per contact point.
    CHECK_FALSE(Spot::DebugDraw::GetLines().empty());
    CHECK(Spot::DebugDraw::GetLines().size() % 8 == 0);
    Spot::DebugDraw::Clear();

    scene.GetOrAddSystem<Spot::PhysicsWorld>().SetDebugDraw(Spot::PhysicsDebugFlags::Queries);
    scene.GetOrAddSystem<Spot::PhysicsWorld>().Raycast({3.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f);
    scene.PrepareToRender();
    // The ray to its hit, a cross and an arrow at the hit.
    CHECK(Spot::DebugDraw::GetLines().size() == 1 + 3 + 5);
    Spot::DebugDraw::Clear();
    // Queries show once.
    scene.PrepareToRender();
    CHECK(Spot::DebugDraw::GetLines().empty());

    scene.GetOrAddSystem<Spot::PhysicsWorld>().SetDebugDraw(Spot::PhysicsDebugFlags::Colliders);
    scene.PrepareToRender();
    CHECK(Spot::DebugDraw::GetLines().size() == 12 + 3 * 32);
    Spot::DebugDraw::Clear();
}

TEST_CASE("Box, sphere and capsule colliders fit around a mesh's bounds", "[physics][debug]") {
    Spot::MeshData mesh = Spot::MeshData::CreateCube(2.0f);
    for (Spot::MeshVertex& vertex : mesh.vertices) {
        vertex.position = vertex.position * Vector3{1.0f, 2.0f, 0.5f} + Vector3{0.0f, 2.0f, 0.0f};
    }
    const Spot::BoundingBox bounds = mesh.GetBoundingBox();
    CHECK(bounds.GetSize() == Vector3{2.0f, 4.0f, 1.0f});
    CHECK(bounds.GetCenter() == Vector3{0.0f, 2.0f, 0.0f});

    Scene scene;
    Entity& entity = scene.CreateEntity();
    auto& box = entity.AddComponent<Spot::BoxCollider>();
    REQUIRE(box.FitTo(bounds));
    CHECK(box.GetSize() == Vector3{2.0f, 4.0f, 1.0f});
    CHECK(box.GetCenter() == Vector3{0.0f, 2.0f, 0.0f});
    auto& sphere = entity.AddComponent<Spot::SphereCollider>();
    REQUIRE(sphere.FitTo(bounds));
    CHECK(sphere.GetRadius() == Approx(std::sqrt(21.0f) * 0.5f));
    auto& capsule = entity.AddComponent<Spot::CapsuleCollider>();
    REQUIRE(capsule.FitTo(bounds));
    CHECK(capsule.GetRadius() == 1.0f);
    CHECK(capsule.GetHeight() == 4.0f);
    CHECK_FALSE(entity.AddComponent<Spot::MeshCollider>().FitTo(bounds));
}
