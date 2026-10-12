#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <spot.h>

using namespace Spot;

TEST_CASE("Bone attachments follow poses and offsets while keeping authored transforms", "[animation][attachment]") {
    Scene scene;
    auto& owner = scene.CreateEntity("Rig");
    owner.GetTransform().SetPosition({2, 0, -3});
    owner.GetTransform().SetScale({2, 2, 2});
    auto& animator = owner.AddComponent<Animator>();
    animator.SetSkeleton(std::make_shared<const Skeleton>(Skeleton{
        "Rig", {{"Root", -1, {}}, {"Hand", 0, {{1, 0, 0}}}}, {0, 1}, {Matrix4{}, Matrix4::Translation({-1, 0, 0})}}));
    REQUIRE(animator.Play(std::make_shared<const AnimationClip>(AnimationClip{
        "Reach",
        1,
        {{1, AnimationPath::Translation, AnimationInterpolation::Linear, {0, 1}, {{1, 0, 0, 0}, {1, 2, 0, 0}}}}})));
    auto& weapon = scene.CreateEntity("Weapon");
    weapon.GetTransform().SetPosition({10, 0, 0});
    auto& attachment = weapon.AddComponent<BoneAttachment>();
    attachment.SetAnimator(owner.GetId());
    attachment.SetBone("Hand");
    attachment.SetOffset({{0.25f, 0, 0}});
    auto& child = weapon.CreateChild("Tip");
    child.GetTransform().SetPosition({0.1f, 0, 0});
    const auto authored = scene.SaveToString();
    animator.Seek(0.5f);
    animator.RefreshRenderers();
    CHECK(weapon.GetTransform().GetWorldPosition().x == Catch::Approx(4.5f));
    CHECK(weapon.GetTransform().GetWorldPosition().y == Catch::Approx(2));
    CHECK(child.GetTransform().GetWorldPosition().x == Catch::Approx(4.7f));
    CHECK(weapon.GetTransform().GetPosition().x == 10);
    CHECK(scene.SaveToString() == authored);
    scene.Update(0.25f);
    CHECK(weapon.GetTransform().GetWorldPosition().y == Catch::Approx(3));
    attachment.SetEnabled(false);
    CHECK_FALSE(weapon.GetTransform().HasWorldOverride());
    CHECK(weapon.GetTransform().GetWorldPosition().x == 10);
    attachment.SetEnabled(true);
    scene.Update(0);
    CHECK(weapon.GetTransform().HasWorldOverride());
    owner.Destroy();
    scene.Update(0);
    CHECK_FALSE(weapon.GetTransform().HasWorldOverride());
}

TEST_CASE("World overrides propagate to children, restore local poses and reject attachment cycles",
          "[animation][attachment]") {
    Scene scene;
    auto& parent = scene.CreateEntity();
    parent.GetTransform().SetPosition({1, 0, 0});
    auto& child = parent.CreateChild();
    child.GetTransform().SetPosition({2, 0, 0});
    CHECK(child.GetTransform().GetWorldPosition().x == 3);
    parent.GetTransform().SetWorldOverride(Matrix4::Translation({5, 0, 0}));
    CHECK(child.GetTransform().GetWorldPosition().x == 7);
    parent.GetTransform().ClearWorldOverride();
    CHECK(child.GetTransform().GetWorldPosition().x == 3);
    parent.GetTransform().SetWorldOverride(Matrix4::Translation({5, 0, 0}));
    parent.GetTransform().SetPosition({4, 0, 0});
    CHECK_FALSE(parent.GetTransform().HasWorldOverride());
    CHECK(child.GetTransform().GetWorldPosition().x == 6);
    auto& animator = child.AddComponent<Animator>();
    animator.SetSkeleton(std::make_shared<const Skeleton>(Skeleton{"Rig", {{"Root", -1, {}}}, {0}, {Matrix4{}}}));
    auto& cyclic = parent.AddComponent<BoneAttachment>();
    cyclic.SetAnimator(child.GetId());
    CHECK_FALSE(cyclic.Synchronize());
    CHECK_FALSE(parent.GetTransform().HasWorldOverride());
}

TEST_CASE("Meshes parented to glTF joints get automatic bone attachments", "[animation][attachment][model]") {
    const auto skeleton =
        std::make_shared<const Skeleton>(Skeleton{"Rig",
                                                  {{"Root", -1, {}}, {"Hand", 0, {{0, 1, 0}}}, {"Body", 0, {}}},
                                                  {0, 1},
                                                  {Matrix4{}, Matrix4::Translation({0, -1, 0})}});
    const AssetRef<Mesh> mesh(AssetId::Generate(), nullptr);
    const AssetRef<AnimationClip> clip = std::make_shared<const AnimationClip>(AnimationClip{
        "Reach",
        1,
        {{1, AnimationPath::Translation, AnimationInterpolation::Linear, {0, 1}, {{0, 1, 0, 0}, {2, 1, 0, 0}}}}});
    Model model({{.name = "Root", .joint = true},
                 {.name = "Hand",
                  .transform = Matrix4::Translation({0, 1, 0}),
                  .parent = 0,
                  .mesh = mesh,
                  .materials = {AssetRef<Material>{}},
                  .joint = true},
                 {.name = "Body",
                  .parent = 0,
                  .mesh = mesh,
                  .materials = {AssetRef<Material>{}},
                  .skeleton = skeleton,
                  .animations = {clip}}});
    Scene scene;
    scene.Instantiate(model);
    auto* hand = scene.FindEntity("Hand");
    REQUIRE(hand);
    REQUIRE(hand->GetComponent<BoneAttachment>());
    scene.Update(0.5f);
    CHECK(hand->GetTransform().GetWorldPosition().x == Catch::Approx(1));
}
