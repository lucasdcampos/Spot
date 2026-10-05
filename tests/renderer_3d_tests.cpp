#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>

using Spot::Camera3D;
using Spot::MeshData;
using Spot::Vector3;

namespace {

constexpr float Tolerance = 1e-4f;

bool IsNear(Vector3 a, Vector3 b) {
    return std::abs(a.x - b.x) < Tolerance && std::abs(a.y - b.y) < Tolerance && std::abs(a.z - b.z) < Tolerance;
}

Vector3 ToClip(const Spot::Matrix4& m, Vector3 point) {
    const auto clip = m * Spot::Vector4{point.x, point.y, point.z, 1.0f};
    return Vector3{clip.x, clip.y, clip.z} / clip.w;
}

// Triangles must wind counterclockwise seen from outside, so their geometric normal agrees with their vertices'.
void CheckFacesPointOut(const MeshData& mesh) {
    REQUIRE(mesh.indices.size() % 3 == 0);
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        const Vector3 faceNormal = Spot::Cross(b.position - a.position, c.position - a.position);
        // The sphere's poles have degenerate triangles.
        if (Spot::LengthSquared(faceNormal) > 1e-10f) {
            CHECK(Spot::Dot(faceNormal, a.normal + b.normal + c.normal) > 0.0f);
        }
    }
}

} // namespace

TEST_CASE("Quaternion::LookRotation turns -Z toward the direction", "[math]") {
    CHECK(IsNear(Spot::Quaternion::LookRotation({1, 0, 0}) * Vector3{0, 0, -1}, {1, 0, 0}));
    CHECK(IsNear(Spot::Quaternion::LookRotation({0, 0, 1}) * Vector3{0, 1, 0}, {0, 1, 0}));
    // Straight down is parallel to the default up, so another up is used instead of producing NaN.
    CHECK(IsNear(Spot::Quaternion::LookRotation({0, -2, 0}) * Vector3{0, 0, -1}, {0, -1, 0}));
}

TEST_CASE("Camera3D projects what it looks at to the center of the view", "[renderer3d]") {
    Camera3D camera;
    camera.position = {0.0f, 2.0f, 5.0f};
    camera.LookAt({0.0f, 2.0f, 0.0f});
    const auto viewProjection = camera.GetViewProjection({800.0f, 600.0f});

    const Vector3 center = ToClip(viewProjection, {0.0f, 2.0f, 0.0f});
    CHECK(center.x == Catch::Approx(0.0f).margin(Tolerance));
    CHECK(center.y == Catch::Approx(0.0f).margin(Tolerance));
    CHECK(center.z > 0.0f);
    CHECK(center.z < 1.0f);

    // Points to the right and above stay there on screen, and nearer points have smaller depth.
    CHECK(ToClip(viewProjection, {1.0f, 2.0f, 0.0f}).x > 0.0f);
    CHECK(ToClip(viewProjection, {0.0f, 3.0f, 0.0f}).y > 0.0f);
    CHECK(ToClip(viewProjection, {0.0f, 2.0f, 2.0f}).z < center.z);

    CHECK(IsNear(camera.GetForward(), {0.0f, 0.0f, -1.0f}));
    CHECK(IsNear(camera.GetRight(), {1.0f, 0.0f, 0.0f}));
    CHECK(IsNear(camera.GetUp(), {0.0f, 1.0f, 0.0f}));
}

TEST_CASE("Camera3D maps the near and far planes to depth 0 and 1", "[renderer3d]") {
    Camera3D camera;
    camera.nearPlane = 0.5f;
    camera.farPlane = 50.0f;
    const auto viewProjection = camera.GetViewProjection({100.0f, 100.0f});

    CHECK(ToClip(viewProjection, {0.0f, 0.0f, -0.5f}).z == Catch::Approx(0.0f).margin(Tolerance));
    CHECK(ToClip(viewProjection, {0.0f, 0.0f, -50.0f}).z == Catch::Approx(1.0f).margin(Tolerance));
}

TEST_CASE("MeshData builds primitives with outward faces", "[renderer3d]") {
    const auto cube = MeshData::CreateCube(2.0f);
    CHECK(cube.vertices.size() == 24);
    CHECK(cube.indices.size() == 36);
    for (const auto& vertex : cube.vertices) {
        CHECK(Spot::Dot(vertex.position, vertex.normal) == Catch::Approx(1.0f));
    }
    CheckFacesPointOut(cube);

    const auto plane = MeshData::CreatePlane(4.0f);
    CHECK(plane.indices.size() == 6);
    CHECK(IsNear(plane.vertices[2].position, {2.0f, 0.0f, 2.0f}));
    CheckFacesPointOut(plane);

    const auto sphere = MeshData::CreateSphere(1.5f, 8, 4);
    CHECK(sphere.vertices.size() == 9 * 5);
    CHECK(sphere.indices.size() == 8 * 4 * 6);
    for (const auto& vertex : sphere.vertices) {
        CHECK(Spot::Length(vertex.position) == Catch::Approx(1.5f));
    }
    CheckFacesPointOut(sphere);
}

TEST_CASE("MeshData recalculates normals from its faces", "[renderer3d]") {
    auto cube = MeshData::CreateCube();
    const auto original = cube.vertices;
    for (auto& vertex : cube.vertices) {
        vertex.normal = {};
    }
    cube.RecalculateNormals();
    for (std::size_t i = 0; i < original.size(); ++i) {
        CHECK(IsNear(cube.vertices[i].normal, original[i].normal));
    }
}
