#include <catch2/catch_test_macros.hpp>
#include <spot.h>

#include <memory>
#include <vector>

using namespace Spot;

namespace {

Vector3 GetPosition(const Matrix4& matrix) {
    return {matrix.columns[3].x, matrix.columns[3].y, matrix.columns[3].z};
}

// An arm hanging down from the origin, a unit long above and below the elbow.
Skeleton MakeArm() {
    Skeleton skeleton{"Arm",
                      {{"Root", -1, {}}, {"Upper", 0, {}}, {"Lower", 1, {{0, -1, 0}}}, {"Hand", 2, {{0, -1, 0}}}},
                      {0, 1, 2, 3},
                      {}};
    for (const float y : {0.0f, 0.0f, 1.0f, 2.0f}) {
        skeleton.inverseBindMatrices.push_back(Matrix4::Translation({0, y, 0}));
    }
    return skeleton;
}

struct ArmPose {
    Skeleton skeleton = MakeArm();
    std::vector<TransformComponents> local = std::vector<TransformComponents>(4);
    std::vector<Matrix4> model = std::vector<Matrix4>(4);
    ArmPose() {
        Pose::Bind(skeleton, local);
        Pose::ToModel(skeleton, local, model);
    }
};

} // namespace

TEST_CASE("Two bone IK reaches goals, keeps bone lengths and bends toward the pole", "[animation][ik]") {
    ArmPose arm;
    const Vector3 goal{1, -0.5f, -0.5f};
    REQUIRE(Pose::SolveTwoBone(arm.skeleton, 3, {.position = goal, .pole = {0, 0, 1}}, arm.local, arm.model));
    const Vector3 elbow = GetPosition(arm.model[2]);
    const Vector3 hand = GetPosition(arm.model[3]);
    CHECK(Distance(hand, goal) < 0.001f);
    CHECK(std::abs(Length(elbow) - 1) < 0.001f);
    CHECK(std::abs(Distance(elbow, hand) - 1) < 0.001f);
    // The elbow sits on the pole's side of the line to the goal.
    const Vector3 line = Normalize(goal);
    CHECK(Dot(elbow - line * Dot(elbow, line), {0, 0, 1}) > 0.1f);

    const Quaternion turned = Quaternion::FromAxisAngle({1, 0, 0}, 0.5f);
    REQUIRE(Pose::SolveTwoBone(arm.skeleton, 3, {.position = {0, -1, -1}, .rotation = turned}, arm.local, arm.model));
    const Quaternion handRotation = Decompose(arm.model[3]).rotation;
    CHECK(std::abs(Dot(Vector4{handRotation.x, handRotation.y, handRotation.z, handRotation.w},
                       Vector4{turned.x, turned.y, turned.z, turned.w})) > 0.999f);

    // Out of reach, the arm points straight at the goal.
    REQUIRE(Pose::SolveTwoBone(arm.skeleton, 3, {.position = {5, 0, 0}}, arm.local, arm.model));
    CHECK(Distance(GetPosition(arm.model[3]), {2, 0, 0}) < 0.01f);

    ArmPose half;
    REQUIRE(Pose::SolveTwoBone(half.skeleton, 3, {.position = {2, 0, 0}, .weight = 0}, half.local, half.model));
    CHECK(Distance(GetPosition(half.model[3]), {0, -2, 0}) < 0.001f);
    CHECK_FALSE(Pose::SolveTwoBone(half.skeleton, 1, {.position = {1, 0, 0}}, half.local, half.model));
}

TEST_CASE("Aim IK spreads a turn along a chain and aims its last bone", "[animation][ik]") {
    const Skeleton skeleton{"Spine",
                            {{"Pelvis", -1, {}}, {"Spine", 0, {{0, 1, 0}}}, {"Chest", 1, {{0, 1, 0}}}},
                            {0, 1, 2},
                            {Matrix4{}, Matrix4::Translation({0, -1, 0}), Matrix4::Translation({0, -2, 0})}};
    std::vector<TransformComponents> local(3);
    std::vector<Matrix4> model(3);
    Pose::Bind(skeleton, local);
    Pose::ToModel(skeleton, local, model);
    const std::vector<Pose::AimBone> chain{{1, 0.5f}, {2, 1}};
    const Vector3 target{0, 12, -10};
    Pose::SolveAim(skeleton, chain, {0, 0, -1}, target, 1, Pi, local, model);
    const Vector3 aimed = Decompose(model[2]).rotation * Vector3{0, 0, -1};
    CHECK(Dot(aimed, Normalize(target - GetPosition(model[2]))) > 0.999f);
    // Both bones turn.
    CHECK(Dot(Decompose(model[1]).rotation * Vector3{0, 0, -1}, {0, 0, -1}) < 0.99f);
    CHECK(Dot(Decompose(model[1]).rotation * Vector3{0, 0, -1}, aimed) < 0.99f);

    Pose::Bind(skeleton, local);
    Pose::ToModel(skeleton, local, model);
    Pose::SolveAim(skeleton, chain, {0, 0, -1}, {0, 102, -1}, 1, 0.5f, local, model);
    CHECK(std::abs(std::acos(Dot(Decompose(model[2]).rotation * Vector3{0, 0, -1}, {0, 0, -1})) - 0.5f) < 0.01f);
}

TEST_CASE("IK constraints turn and reach an animator's pose after animation, attachments in between",
          "[animation][ik]") {
    Scene scene;
    auto& owner = scene.CreateEntity("Rig");
    owner.GetTransform().SetPosition({2, 0, 0});
    auto& animator = owner.AddComponent<Animator>();
    animator.SetSkeleton(std::make_shared<const Skeleton>(MakeArm()));
    // The root turns to face +X, and the grip hangs ahead of it.
    auto& aim = owner.AddComponent<AimIK>();
    aim.SetBones({{"Root", 1}});
    aim.SetTargetPosition({12, 0, 0});
    auto& mount = scene.CreateEntity("Mount");
    auto& attachment = mount.AddComponent<BoneAttachment>();
    attachment.SetAnimator(owner.GetId());
    attachment.SetBone("Root");
    attachment.SetOffset({{0, -1, -1}});
    auto& grip = mount.CreateChild("Grip");
    auto& reach = owner.CreateChild("Hand Reach").AddComponent<TwoBoneIK>();
    reach.SetBone("Hand");
    reach.SetTarget(grip.GetId());
    reach.SetMatchingRotation(false);
    scene.Update(1.0f / 60);
    const auto handWorld = [&] {
        return GetPosition(owner.GetTransform().GetWorldMatrix() * animator.GetModelPose()[3]);
    };
    CHECK(Distance(grip.GetTransform().GetWorldPosition(), {3, -1, 0}) < 0.001f);
    CHECK(Distance(handWorld(), {3, -1, 0}) < 0.001f);

    reach.SetWeight(0);
    aim.SetEnabled(false);
    scene.Update(1.0f / 60);
    CHECK(Distance(handWorld(), {2, -2, 0}) < 0.001f);

    reach.SetWeight(1);
    animator.ApplyConstraints();
    CHECK(Distance(handWorld(), grip.GetTransform().GetWorldPosition()) < 0.001f);
}
