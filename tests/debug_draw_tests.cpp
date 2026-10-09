#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace DebugDraw = Spot::DebugDraw;
using Spot::Vector3;

namespace {

// Starts every test from an empty queue, and leaves it empty.
struct CleanQueue {
    CleanQueue() { DebugDraw::Clear(); }
    ~CleanQueue() { DebugDraw::Clear(); }
};

} // namespace

TEST_CASE("Debug shapes last a frame, or as long as asked", "[debug_draw]") {
    const CleanQueue clean;
    DebugDraw::Line({}, {1.0f, 0.0f, 0.0f});
    DebugDraw::Line({}, {0.0f, 1.0f, 0.0f}, {.color = Spot::Color::Red, .duration = 1.0f, .depthTest = false});
    REQUIRE(DebugDraw::GetLines().size() == 2);
    const Spot::DebugLine& lasting = DebugDraw::GetLines()[1];
    CHECK(lasting.to == Vector3{0.0f, 1.0f, 0.0f});
    CHECK(lasting.color == Spot::Color::Red);
    CHECK_FALSE(lasting.depthTest);

    // The frame's line goes once the frame was drawn; the other stays for its second.
    DebugDraw::Update(0.5f);
    REQUIRE(DebugDraw::GetLines().size() == 1);
    CHECK(DebugDraw::GetLines()[0].color == Spot::Color::Red);
    DebugDraw::Update(0.4f);
    CHECK(DebugDraw::GetLines().size() == 1);
    DebugDraw::Update(0.2f);
    CHECK(DebugDraw::GetLines().empty());

    // A frame without time still ends lines meant for it.
    DebugDraw::Cross({}, 1.0f);
    DebugDraw::Update(0.0f);
    CHECK(DebugDraw::GetLines().empty());
}

TEST_CASE("Debug shapes are made of lines", "[debug_draw]") {
    const CleanQueue clean;
    const auto count = [](auto draw) {
        DebugDraw::Clear();
        draw();
        return DebugDraw::GetLines().size();
    };
    CHECK(count([] { DebugDraw::Cross({}, 1.0f); }) == 3);
    CHECK(count([] { DebugDraw::Arrow({}, {0.0f, 0.0f, 2.0f}); }) == 5);
    CHECK(count([] { DebugDraw::Box({}, {1.0f, 2.0f, 3.0f}); }) == 12);
    CHECK(count([] { DebugDraw::Circle({}, {0.0f, 1.0f, 0.0f}, 1.0f); }) == 32);
    CHECK(count([] { DebugDraw::Sphere({}, 1.0f); }) == 96);
    CHECK(count([] { DebugDraw::Capsule({}, {0.0f, 2.0f, 0.0f}, 0.5f); }) == 2 * 32 + 4 + 4 * 16);

    // Boxes turn with their rotation, around their center.
    DebugDraw::Clear();
    DebugDraw::Box({0.0f, 5.0f, 0.0f}, {2.0f, 2.0f, 2.0f},
                   Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, Spot::Pi / 4.0f));
    float largestX = 0.0f;
    for (const Spot::DebugLine& line : DebugDraw::GetLines()) {
        largestX = std::max(largestX, line.from.x);
        CHECK(std::abs(line.from.y - 5.0f) <= 1.0f + 1e-5f);
    }
    CHECK(largestX > 1.4f);

    // Points on a sphere's circles are a radius away from its center.
    DebugDraw::Clear();
    DebugDraw::Sphere({1.0f, 2.0f, 3.0f}, 2.0f);
    for (const Spot::DebugLine& line : DebugDraw::GetLines()) {
        CHECK(std::abs(Spot::Distance(line.from, Vector3{1.0f, 2.0f, 3.0f}) - 2.0f) < 1e-4f);
    }
}
