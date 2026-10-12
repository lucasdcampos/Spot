#include "temp_directory.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <spot.h>
#include <spot_import.h>

#include <array>

using namespace Spot;

namespace {
void SetRig(Animator& animator) {
    animator.SetSkeleton(std::make_shared<const Skeleton>(Skeleton{
        "Rig", {{"Root", -1, {}}, {"Hand", 0, {{0, 1, 0}}}}, {0, 1}, {Matrix4{}, Matrix4::Translation({0, -1, 0})}}));
    REQUIRE(animator.Play(std::make_shared<const AnimationClip>(AnimationClip{"Idle", 1, {}})));
}
const std::array Bones{RagdollBone{"Root", {0.3f, 0.3f, 0.3f}, {}, 5},
                       RagdollBone{"Hand", {0.2f, 0.3f, 0.2f}, {}, 2, {0, 1, 0}, 0.3f, -0.2f, 0.2f}};
} // namespace

TEST_CASE("Ragdolls build limited joints, follow physics and recover without duplicating bodies",
          "[animation][ragdoll]") {
    Scene scene;
    auto& actor = scene.CreateEntity("Actor");
    actor.GetTransform().SetPosition({0, 3, 0});
    auto& animator = actor.AddComponent<Animator>();
    SetRig(animator);
    auto& character = actor.AddComponent<CharacterController>();
    auto& ragdoll = actor.AddComponent<Ragdoll>();
    REQUIRE(ragdoll.SetBones(Bones));
    REQUIRE(ragdoll.Build());
    CHECK(ragdoll.GetBodies().size() == 2);
    const std::vector<EntityId> ids(ragdoll.GetBodies().begin(), ragdoll.GetBodies().end());
    auto* hand = scene.GetEntity(ids[1]);
    REQUIRE(hand);
    const auto* joint = hand->GetComponent<SwingTwistJoint>();
    REQUIRE(joint);
    CHECK(joint->GetConnectedEntity() == ids[0]);
    CHECK(joint->GetSwingLimit() == Catch::Approx(0.3f));
    CHECK(joint->GetTwistMin() == Catch::Approx(-0.2f));
    CHECK_FALSE(hand->IsActiveInHierarchy());
    REQUIRE(ragdoll.SetActive(true));
    CHECK_FALSE(character.IsEnabled());
    CHECK_FALSE(animator.IsPlaying());
    ragdoll.AddImpulse({5, 0, 0}, {0, 3, 0});
    for (int i = 0; i < 30; ++i) {
        scene.Update(1.0f / 60);
    }
    CHECK(actor.GetTransform().GetWorldPosition().y < 2.5f);
    CHECK(actor.GetTransform().GetWorldPosition().x > 0.1f);
    const auto bodyWorld = scene.GetEntity(ids[1])->GetTransform().GetWorldMatrix();
    const auto animatedWorld = actor.GetTransform().GetWorldMatrix() * animator.GetModelPose()[1];
    CHECK(animatedWorld.columns[3].y == Catch::Approx(bodyWorld.columns[3].y).margin(1e-4f));
    REQUIRE(ragdoll.SetActive(false, 0.2f));
    CHECK(character.IsEnabled());
    CHECK(animator.IsPlaying());
    CHECK_FALSE(scene.GetEntity(ids[0])->IsActiveInHierarchy());
    scene.Update(0.25f);
    CHECK(animator.GetLocalPose()[1].translation.y == Catch::Approx(1));
    REQUIRE(ragdoll.SetActive(true));
    CHECK(std::vector<EntityId>(ragdoll.GetBodies().begin(), ragdoll.GetBodies().end()) == ids);
    CHECK(scene.GetEntityCount() == 3);
    actor.Destroy();
    CHECK(scene.GetEntity(ids[0]) == nullptr);
    CHECK(scene.GetEntity(ids[1]) == nullptr);
}

TEST_CASE("Ragdolls reject invalid and duplicate bone configurations atomically", "[animation][ragdoll]") {
    Scene scene;
    auto& actor = scene.CreateEntity();
    auto& animator = actor.AddComponent<Animator>();
    SetRig(animator);
    auto& ragdoll = actor.AddComponent<Ragdoll>();
    REQUIRE(ragdoll.SetBones(Bones));
    auto invalid = Bones;
    invalid[1].bone = "Missing";
    CHECK_FALSE(ragdoll.SetBones(invalid));
    CHECK(ragdoll.GetBones()[1].bone == "Hand");
    invalid = Bones;
    invalid[1].bone = "Root";
    CHECK_FALSE(ragdoll.SetBones(invalid));
    invalid = Bones;
    invalid[0].mass = -1;
    CHECK_FALSE(ragdoll.SetBones(invalid));
    CHECK(scene.GetEntityCount() == 1);
}

TEST_CASE("Ragdoll saves restore body identities and physics mode without new proxies",
          "[animation][ragdoll][assets]") {
    TempDirectory directory;
    const auto source = directory.GetPath() / "assets";
    std::filesystem::create_directories(source);
    std::filesystem::copy_file(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "models/rig.gltf", source / "rig.gltf");
    AssetDatabase database(source, directory.GetPath() / "imported");
    database.Refresh();
    AssetManager assets(directory.GetPath() / "imported");
    Scene scene;
    auto& actor = scene.CreateEntity("Actor");
    actor.GetTransform().SetPosition({0, 3, 0});
    auto& animator = actor.AddComponent<Animator>();
    animator.SetSkeleton(assets.Load<Skeleton>("rig.gltf#Rig"));
    REQUIRE(animator.Play(assets.Load<AnimationClip>("rig.gltf#Move")));
    auto& ragdoll = actor.AddComponent<Ragdoll>();
    REQUIRE(ragdoll.SetBones(Bones));
    REQUIRE(ragdoll.SetActive(true));
    scene.Update(1.0f / 60);
    const auto count = scene.GetEntityCount();
    Scene restored;
    REQUIRE(restored.LoadFromString(scene.SaveToString(), &assets));
    auto* restoredActor = restored.FindEntity("Actor");
    REQUIRE(restoredActor);
    auto* restoredRagdoll = restoredActor->GetComponent<Ragdoll>();
    REQUIRE(restoredRagdoll);
    restored.Update(1.0f / 60);
    CHECK(restoredRagdoll->IsActive());
    CHECK(restoredRagdoll->GetBodies().size() == 2);
    CHECK(restored.GetEntityCount() == count);
    for (const auto id : restoredRagdoll->GetBodies()) {
        CHECK(restored.GetEntity(id));
    }
}

