#include <spot/physics/physics_3d.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace {
constexpr float Step = 1.0f / 60.0f;
// Resting bodies may penetrate the supporting surface by this much.
constexpr float RestMargin = 0.03f;
} // namespace

TEST_CASE("3D physics simulates bodies, shapes, contacts and queries without a scene", "[physics]") {
    Spot::Physics3D physics;
    const Spot::BodyId3D floor = physics.CreateBody(
        {.position = {0.0f, -0.5f, 0.0f}, .shapes = {Spot::Shape3D{.halfExtents = {10.0f, 0.5f, 10.0f}}}});
    REQUIRE(floor != Spot::BodyId3D::Invalid);

    for (const auto kind : {Spot::ShapeKind3D::Box, Spot::ShapeKind3D::Sphere, Spot::ShapeKind3D::Capsule}) {
        const Spot::BodyId3D body = physics.CreateBody({.motion = Spot::BodyMotion3D::Dynamic,
                                                        .position = {0.0f, 3.0f, 0.0f},
                                                        .shapes = {Spot::Shape3D{.kind = kind}}});
        REQUIRE(body != Spot::BodyId3D::Invalid);

        for (int i = 0; i < 180; ++i) {
            physics.Step(Step);
        }

        const float expectedY = kind == Spot::ShapeKind3D::Capsule ? 1.0f : 0.5f;
        CHECK(physics.GetPose(body).position.y == Approx(expectedY).margin(RestMargin));

        std::vector<Spot::Contact3D> contacts;
        physics.GetContacts(contacts);
        CHECK_FALSE(contacts.empty());

        Spot::QueryHit3D hit;
        REQUIRE(physics.Raycast({0.0f, 5.0f, 0.0f}, {0.0f, -2.0f, 0.0f}, 10.0f, {}, hit));
        CHECK(hit.body == body);
        CHECK(hit.distance == Approx(Spot::Distance({0.0f, 5.0f, 0.0f}, hit.point)));
        REQUIRE(physics.Raycast({0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f, {.ignore = body}, hit));
        CHECK(hit.body == floor);

        std::vector<Spot::QueryHit3D> hits;
        physics.RaycastAll({0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f, {}, hits);
        CHECK(hits.size() == 2);
        REQUIRE(physics.SphereCast({-3.0f, expectedY, 0.0f}, 0.25f, {1.0f, 0.0f, 0.0f}, 10.0f, {.ignore = floor}, hit));
        CHECK(hit.body == body);
        physics.OverlapSphere({0.0f, expectedY, 0.0f}, 0.2f, {.ignore = floor}, hits);
        CHECK(hits.size() == 1);
        physics.OverlapBox({0.0f, expectedY, 0.0f}, {0.2f, 0.2f, 0.2f}, {}, {.ignore = floor}, hits);
        CHECK(hits.size() == 1);

        physics.DestroyBody(body);
    }

    const Spot::BodyId3D kinematic =
        physics.CreateBody({.motion = Spot::BodyMotion3D::Kinematic, .shapes = {Spot::Shape3D{}}});
    REQUIRE(kinematic != Spot::BodyId3D::Invalid);
    physics.MoveKinematic(kinematic, {.position = {2.0f, 1.0f, 0.0f}}, Step);
    physics.Step(Step);
    CHECK(physics.GetPose(kinematic).position.x == Approx(2.0f));
    physics.DestroyBody(kinematic);
    physics.DestroyBody(floor);
}
