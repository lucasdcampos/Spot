#include "viewport/transform_gizmo.h"

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;
using Spot::Matrix4;
using Spot::Quaternion;
using Spot::Vector3;
using Spot::Editor::GizmoTarget;
using Spot::Editor::GizmoTool;

namespace {

bool IsNear(Vector3 a, Vector3 b) {
    return std::abs(a.x - b.x) < 1e-4f && std::abs(a.y - b.y) < 1e-4f && std::abs(a.z - b.z) < 1e-4f;
}

bool IsSameRotation(const Quaternion& a, const Quaternion& b) {
    return std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) == Approx(1.0f).margin(1e-5);
}

// An entity at the root, whose local pose is its world pose.
GizmoTarget AtRoot(Vector3 position, const Quaternion& rotation = {}, Vector3 scale = {1.0f, 1.0f, 1.0f}) {
    return {{position, rotation, scale}, position, rotation, Matrix4::Identity(), Quaternion::Identity()};
}

const Quaternion QuarterTurn = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, Spot::ToRadians(90.0f));

} // namespace

TEST_CASE("Moving the gizmo moves entities by as much, in their parents' space", "[editor][gizmo]") {
    const Matrix4 start = Matrix4::Translation({1.0f, 0.0f, 0.0f});
    const Matrix4 current = Matrix4::Translation({3.0f, 0.0f, 0.0f});
    const Spot::Editor::Pose moved =
        Spot::Editor::ApplyGizmoChange(GizmoTool::Move, AtRoot({1.0f, 0.0f, 0.0f}, QuarterTurn), start, current);
    CHECK(IsNear(moved.position, {3.0f, 0.0f, 0.0f}));
    CHECK(moved.rotation == QuarterTurn);

    // A child of a parent that is moved, turned and scaled moves up by half as much in its parent's space.
    const Matrix4 parent =
        Matrix4::Translation({10.0f, 0.0f, 0.0f}) * Matrix4::Rotation(QuarterTurn) * Matrix4::Scale({2.0f, 2.0f, 2.0f});
    const Spot::Vector4 world = parent * Spot::Vector4{1.0f, 0.0f, 0.0f, 1.0f};
    const GizmoTarget child{
        {{1.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}}, {world.x, world.y, world.z}, QuarterTurn, parent, QuarterTurn};
    const Matrix4 childStart = parent * Matrix4::Translation({1.0f, 0.0f, 0.0f});
    const Spot::Editor::Pose raised = Spot::Editor::ApplyGizmoChange(
        GizmoTool::Move, child, childStart, Matrix4::Translation({0.0f, 1.0f, 0.0f}) * childStart);
    CHECK(IsNear(raised.position, {1.0f, 0.5f, 0.0f}));
}

TEST_CASE("Turning the gizmo turns entities around its pivot", "[editor][gizmo]") {
    const Matrix4 start = Matrix4::Identity();
    const Matrix4 current = Matrix4::Rotation(QuarterTurn);

    const Spot::Editor::Pose pivot = Spot::Editor::ApplyGizmoChange(GizmoTool::Rotate, AtRoot({}), start, current);
    CHECK(IsNear(pivot.position, {}));
    CHECK(IsSameRotation(pivot.rotation, QuarterTurn));

    // Another selected entity swings around the pivot, and turns as much.
    const Spot::Editor::Pose other =
        Spot::Editor::ApplyGizmoChange(GizmoTool::Rotate, AtRoot({2.0f, 0.0f, 0.0f}), start, current);
    CHECK(IsNear(other.position, {0.0f, 0.0f, -2.0f}));
    CHECK(IsSameRotation(other.rotation, QuarterTurn));
}

TEST_CASE("Scaling the gizmo scales entities by the same factors", "[editor][gizmo]") {
    const Matrix4 start = Matrix4::Rotation(QuarterTurn);
    const Matrix4 current = Matrix4::Rotation(QuarterTurn) * Matrix4::Scale({2.0f, 1.0f, 0.5f});
    const GizmoTarget target = AtRoot({4.0f, 0.0f, 0.0f}, {}, {3.0f, 3.0f, 3.0f});
    const Spot::Editor::Pose scaled = Spot::Editor::ApplyGizmoChange(GizmoTool::Scale, target, start, current);
    CHECK(IsNear(scaled.scale, {6.0f, 3.0f, 1.5f}));
    CHECK(IsNear(scaled.position, {4.0f, 0.0f, 0.0f}));
}
