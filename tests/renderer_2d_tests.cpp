#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Spot::Camera2D;
using Spot::QuadBatch;
using Spot::Vector2;

namespace {

constexpr float Tolerance = 1e-4f;

bool IsNear(Vector2 a, Vector2 b) {
    return std::abs(a.x - b.x) < Tolerance && std::abs(a.y - b.y) < Tolerance;
}

Vector2 ToClip(const Spot::Matrix4& m, Vector2 point) {
    const auto clip = m * Spot::Vector4{point.x, point.y, 0.0f, 1.0f};
    return {clip.x / clip.w, clip.y / clip.w};
}

} // namespace

TEST_CASE("Camera2D maps the view around its position to clip space", "[renderer2d]") {
    Camera2D camera;
    camera.position = {100.0f, 50.0f};
    const Vector2 view{800.0f, 600.0f};
    const auto viewProjection = camera.GetViewProjection(view);

    CHECK(IsNear(ToClip(viewProjection, {100.0f, 50.0f}), {0.0f, 0.0f}));
    CHECK(IsNear(ToClip(viewProjection, {500.0f, 350.0f}), {1.0f, 1.0f}));
    CHECK(IsNear(ToClip(viewProjection, {-300.0f, -250.0f}), {-1.0f, -1.0f}));

    camera.zoom = 2.0f;
    CHECK(IsNear(ToClip(camera.GetViewProjection(view), {300.0f, 200.0f}), {1.0f, 1.0f}));
}

TEST_CASE("Camera2D converts screen positions back to the world", "[renderer2d]") {
    Camera2D camera;
    camera.position = {10.0f, 20.0f};
    const Vector2 view{800.0f, 600.0f};

    CHECK(IsNear(camera.ScreenToWorld({400.0f, 300.0f}, view), {10.0f, 20.0f}));
    // Screen Y grows downward, world Y upward.
    CHECK(IsNear(camera.ScreenToWorld({0.0f, 0.0f}, view), {-390.0f, 320.0f}));

    camera.zoom = 2.0f;
    camera.rotation = Spot::Pi / 2.0f;
    const Vector2 screen{600.0f, 300.0f};
    const Vector2 world = camera.ScreenToWorld(screen, view);
    CHECK(IsNear(world, {10.0f, 120.0f}));
    CHECK(IsNear(ToClip(camera.GetViewProjection(view), world), {0.5f, 0.0f}));
}

TEST_CASE("QuadBatch builds four vertices per quad, top-left first", "[renderer2d]") {
    QuadBatch batch;
    batch.AddQuad({10.0f, 20.0f}, {4.0f, 2.0f}, 0.0f, nullptr, Spot::Color::Red);

    REQUIRE(batch.GetVertices().size() == 4);
    const auto vertices = batch.GetVertices();
    CHECK(IsNear(vertices[0].position, {8.0f, 21.0f}));
    CHECK(IsNear(vertices[1].position, {12.0f, 21.0f}));
    CHECK(IsNear(vertices[2].position, {12.0f, 19.0f}));
    CHECK(IsNear(vertices[3].position, {8.0f, 19.0f}));
    CHECK(vertices[0].uv == Vector2{0.0f, 0.0f});
    CHECK(vertices[2].uv == Vector2{1.0f, 1.0f});
    CHECK(vertices[3].color == Spot::Color::Red);
}

TEST_CASE("QuadBatch rotates quads counterclockwise around their center", "[renderer2d]") {
    QuadBatch batch;
    batch.AddQuad({0.0f, 0.0f}, {2.0f, 2.0f}, Spot::Pi / 2.0f, nullptr, Spot::Color::White);

    // The top-left corner (-1, 1) turns a quarter to (-1, -1).
    CHECK(IsNear(batch.GetVertices()[0].position, {-1.0f, -1.0f}));
}

TEST_CASE("QuadBatch merges consecutive quads that share a texture", "[renderer2d]") {
    // Only the addresses are compared, so stand-ins for textures are enough.
    const auto* first = reinterpret_cast<const Spot::Texture*>(0x10);
    const auto* second = reinterpret_cast<const Spot::Texture*>(0x20);

    QuadBatch batch;
    batch.AddQuad({}, {1.0f, 1.0f}, 0.0f, nullptr, Spot::Color::White);
    batch.AddQuad({}, {1.0f, 1.0f}, 0.0f, nullptr, Spot::Color::White);
    batch.AddQuad({}, {1.0f, 1.0f}, 0.0f, first, Spot::Color::White);
    batch.AddQuad({}, {1.0f, 1.0f}, 0.0f, second, Spot::Color::White);
    batch.AddQuad({}, {1.0f, 1.0f}, 0.0f, second, Spot::Color::White);

    const auto commands = batch.GetCommands();
    REQUIRE(commands.size() == 3);
    CHECK(commands[0].texture == nullptr);
    CHECK(commands[0].firstQuad == 0);
    CHECK(commands[0].quadCount == 2);
    CHECK(commands[1].texture == first);
    CHECK(commands[1].firstQuad == 2);
    CHECK(commands[2].texture == second);
    CHECK(commands[2].quadCount == 2);
    CHECK(batch.GetQuadCount() == 5);

    batch.Clear();
    CHECK(batch.IsEmpty());
    CHECK(batch.GetCommands().empty());
}
