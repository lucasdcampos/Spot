#include <spot/render/light_clusters.h>
#include <spot/render/shadow_views.h>

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

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

TEST_CASE("Orthographic cameras see things the same size however far they are", "[renderer3d]") {
    Camera3D camera;
    camera.orthographic = true;
    camera.orthographicSize = 2.0f;
    camera.nearPlane = 1.0f;
    camera.farPlane = 21.0f;
    // Twice as wide as tall: the view spans 8 units across and 4 up.
    const auto viewProjection = camera.GetViewProjection({200.0f, 100.0f});

    const Vector3 corner = ToClip(viewProjection, {4.0f, 2.0f, -1.0f});
    CHECK(corner.x == Catch::Approx(1.0f).margin(Tolerance));
    CHECK(corner.y == Catch::Approx(1.0f).margin(Tolerance));
    CHECK(corner.z == Catch::Approx(0.0f).margin(Tolerance));
    const Vector3 far = ToClip(viewProjection, {4.0f, 2.0f, -21.0f});
    CHECK(far.x == Catch::Approx(1.0f).margin(Tolerance));
    CHECK(far.z == Catch::Approx(1.0f).margin(Tolerance));
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

TEST_CASE("MeshData bounds hold every vertex around the middle of its box", "[renderer3d]") {
    CHECK(MeshData{}.GetBounds().radius == 0.0f);

    auto plane = MeshData::CreatePlane(2.0f);
    for (auto& vertex : plane.vertices) {
        vertex.position += Spot::Vector3{3.0f, 1.0f, 0.0f};
    }
    const auto bounds = plane.GetBounds();
    CHECK(IsNear(bounds.center, {3.0f, 1.0f, 0.0f}));
    CHECK(bounds.radius == Catch::Approx(std::sqrt(2.0f)));
}

TEST_CASE("Shadow cascades end further apart away from the camera, the last at the shadow distance", "[renderer3d]") {
    const auto ends = Spot::Detail::GetCascadeEnds(0.1f, 60.0f, 4);
    CHECK(ends[0] > 0.1f);
    CHECK(ends[1] - ends[0] > ends[0] - 0.1f);
    CHECK(ends[2] - ends[1] > ends[1] - ends[0]);
    CHECK(ends[3] == 60.0f);

    const auto two = Spot::Detail::GetCascadeEnds(0.1f, 60.0f, 2);
    CHECK(two[1] == 60.0f);
    CHECK(two[3] == 60.0f);
}

TEST_CASE("Shadow cascades see their slice of the view and move in whole texels", "[renderer3d]") {
    Camera3D camera;
    camera.position = {1.0f, 2.0f, 3.0f};
    camera.LookAt({4.0f, 1.0f, -5.0f});
    const Vector3 light = Spot::Normalize(Vector3{-0.4f, -1.0f, -0.6f});
    constexpr std::uint32_t Size = 1024;
    const auto view = Spot::Detail::GetCascadeView(camera, 16.0f / 9.0f, 2.0f, 10.0f, light, Size);

    // The corners of the slice, at both of its ends, fall inside the cascade.
    const float tanY = std::tan(camera.fieldOfView * 0.5f);
    const float tanX = tanY * 16.0f / 9.0f;
    for (const float distance : {2.0f, 10.0f}) {
        for (const float x : {-1.0f, 1.0f}) {
            for (const float y : {-1.0f, 1.0f}) {
                const Vector3 corner = camera.position + camera.GetForward() * distance +
                                       camera.GetRight() * (x * tanX * distance) +
                                       camera.GetUp() * (y * tanY * distance);
                const Vector3 clip = ToClip(view.viewProjection, corner);
                CHECK(std::abs(clip.x) <= 1.0f);
                CHECK(std::abs(clip.y) <= 1.0f);
                CHECK(clip.z >= 0.0f);
                CHECK(clip.z <= 1.0f);
            }
        }
    }

    // Moving the camera a little moves the cascade by whole texels, so the same world point lands at the same place
    // within a texel.
    Camera3D moved = camera;
    moved.position += Vector3{0.013f, 0.0f, 0.007f};
    const auto movedView = Spot::Detail::GetCascadeView(moved, 16.0f / 9.0f, 2.0f, 10.0f, light, Size);
    const Vector3 point{3.0f, 0.0f, -4.0f};
    const Vector3 before = ToClip(view.viewProjection, point);
    const Vector3 after = ToClip(movedView.viewProjection, point);
    const float texels = (after.x - before.x) * Size / 2.0f;
    CHECK(std::abs(texels - std::round(texels)) < 0.01f);
    CHECK(view.texelSize == Catch::Approx(movedView.texelSize));
}

TEST_CASE("Point light shadows see every direction from one of their faces", "[renderer3d]") {
    const Spot::LightData light{.type = Spot::LightType::Point, .position = {1.0f, 2.0f, 3.0f}, .range = 10.0f};
    const std::array<Vector3, 6> directions{
        {{1, 0.9f, -0.9f}, {-1, 0.2f, 0.9f}, {0.5f, 1, 0.5f}, {0, -1, 0}, {-0.9f, 0.9f, 1}, {0.3f, -0.3f, -1}}};
    for (std::uint32_t face = 0; face < 6; ++face) {
        const auto view = Spot::Detail::GetPointShadowView(light, face, 256);
        const Vector3 clip = ToClip(view.viewProjection, light.position + Spot::Normalize(directions[face]) * 5.0f);
        CHECK(std::abs(clip.x) < 1.0f);
        CHECK(std::abs(clip.y) < 1.0f);
        CHECK(clip.z > 0.0f);
        CHECK(clip.z < 1.0f);
    }
}

TEST_CASE("Spot light shadows see their whole cone", "[renderer3d]") {
    const Spot::LightData light{.type = Spot::LightType::Spot,
                                .position = {0.0f, 5.0f, 0.0f},
                                .direction = {0.0f, -1.0f, 0.0f},
                                .range = 10.0f,
                                .outerConeAngle = Spot::ToRadians(40.0f)};
    const auto view = Spot::Detail::GetSpotShadowView(light, 512);
    const float edge = 5.0f * std::tan(Spot::ToRadians(40.0f));
    const Vector3 clip = ToClip(view.viewProjection, {edge * 0.999f, 0.0f, 0.0f});
    CHECK(std::abs(clip.x) < 1.0f);
    CHECK(std::abs(clip.y) < 1.0f);
}

TEST_CASE("Spheres are in a view unless wholly outside one of its planes", "[renderer3d]") {
    Camera3D camera;
    const auto viewProjection = camera.GetViewProjection({1.0f, 1.0f});
    CHECK(Spot::Detail::IsSphereInView(viewProjection, {0.0f, 0.0f, -5.0f}, 0.5f));
    CHECK_FALSE(Spot::Detail::IsSphereInView(viewProjection, {0.0f, 0.0f, 5.0f}, 0.5f));
    CHECK(Spot::Detail::IsSphereInView(viewProjection, {0.0f, 0.0f, 0.3f}, 0.5f));
    CHECK_FALSE(Spot::Detail::IsSphereInView(viewProjection, {20.0f, 0.0f, -5.0f}, 0.5f));
    // Just past the left edge, but reaching into the view.
    CHECK(Spot::Detail::IsSphereInView(viewProjection, {-3.3f, 0.0f, -5.0f}, 0.5f));
    CHECK_FALSE(Spot::Detail::IsSphereInView(viewProjection, {0.0f, 0.0f, -2000.0f}, 0.5f));

    const auto ortho = Spot::Matrix4::Orthographic(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 10.0f);
    CHECK_FALSE(Spot::Detail::IsSphereInView(ortho, {0.0f, 0.0f, 5.0f}, 0.5f));
    CHECK(Spot::Detail::IsSphereInView(ortho, {0.0f, 0.0f, 5.0f}, 0.5f, true));
}

TEST_CASE("Light clusters list each light where it may reach, and nowhere far from it", "[renderer3d]") {
    namespace Detail = Spot::Detail;
    Camera3D camera;
    camera.position = {0.0f, 0.0f, 0.0f};
    const Spot::Vector2 view{1600.0f, 900.0f};
    // In front of the camera at 10 units, at the center; behind the camera; and far to the side.
    const std::array<Detail::ClusterLight, 3> lights{{
        {{0.0f, 0.0f, -10.0f}, 1.0f, 0},
        {{0.0f, 0.0f, 10.0f}, 1.0f, 1},
        {{500.0f, 0.0f, -10.0f}, 1.0f, 2},
    }};
    const Detail::LightClusters clusters = Detail::BuildLightClusters(camera, view, lights);
    REQUIRE(clusters.clusters.size() == Detail::ClusterCount);

    const auto lightsAt = [&](std::uint32_t x, std::uint32_t y, float depth) {
        const auto [first, count] =
            clusters.clusters[Detail::GetClusterIndex(x, y, Detail::GetClusterSlice(clusters, depth))];
        return std::vector<std::uint32_t>(clusters.lightIndices.begin() + first,
                                          clusters.lightIndices.begin() + first + count);
    };
    // The light in front is in the clusters at the center of the view around its depth, and only there.
    CHECK(lightsAt(Detail::ClusterTilesX / 2, Detail::ClusterTilesY / 2, 10.0f) == std::vector<std::uint32_t>{0});
    CHECK(lightsAt(Detail::ClusterTilesX / 2, Detail::ClusterTilesY / 2, 50.0f).empty());
    CHECK(lightsAt(Detail::ClusterTilesX / 2, Detail::ClusterTilesY / 2, 2.0f).empty());
    CHECK(lightsAt(0, 0, 10.0f).empty());
    // The others reach no cluster at all.
    std::size_t listed = 0;
    for (const auto& [first, count] : clusters.clusters) {
        listed += count;
    }
    CHECK(listed == clusters.lightIndices.size());
    for (const std::uint32_t index : clusters.lightIndices) {
        CHECK(index == 0);
    }

    // Slices grow with depth, from the near plane, and stop at the last one.
    CHECK(Detail::GetClusterSlice(clusters, 0.0f) == 0);
    CHECK(Detail::GetClusterSlice(clusters, 1.0f) < Detail::GetClusterSlice(clusters, 10.0f));
    CHECK(Detail::GetClusterSlice(clusters, 100000.0f) == Detail::ClusterSlices - 1);

    // A light around the camera covers the whole view at its depths.
    const std::array<Detail::ClusterLight, 1> around{{{{0.0f, 0.0f, 0.0f}, 3.0f, 7}}};
    const Detail::LightClusters near = Detail::BuildLightClusters(camera, view, around);
    const auto [first, count] = near.clusters[Detail::GetClusterIndex(0, Detail::ClusterTilesY - 1, 0)];
    CHECK(count == 1);
}
