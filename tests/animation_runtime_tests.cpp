#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace Spot;

namespace {
AssetRef<Skeleton> MakeSkeleton() {
    return std::make_shared<const Skeleton>(Skeleton{"Rig", {{"Root", -1, {}}}, {0}, {Matrix4{}}});
}
AssetRef<AnimationClip> MakeClip(float end) {
    return std::make_shared<const AnimationClip>(AnimationClip{
        "Move", 1, {{0, AnimationPath::Translation, AnimationInterpolation::Linear, {0, 1}, {{}, {end, 0, 0, 0}}}}});
}
} // namespace

TEST_CASE("Animator scene playback respects speed, loop, pause and activity", "[animation][animator]") {
    Scene scene;
    auto& owner = scene.CreateEntity();
    auto& animator = owner.AddComponent<Animator>();
    animator.SetSkeleton(MakeSkeleton());
    REQUIRE(animator.Play(MakeClip(4), false));
    animator.SetSpeed(2);
    scene.Update(0.25f);
    CHECK(scene.GetSystem<AnimationSystem>() != nullptr);
    CHECK(animator.GetLocalPose()[0].translation.x == Catch::Approx(2));
    animator.SetEnabled(false);
    scene.Update(0.25f);
    CHECK(animator.GetTime() == Catch::Approx(0.5f));
    animator.SetEnabled(true);
    scene.Update(0.3f);
    CHECK_FALSE(animator.IsPlaying());
    CHECK(animator.GetLocalPose()[0].translation.x == 4);
    animator.SetSpeed(-1);
    REQUIRE(animator.Play(MakeClip(4)));
    scene.Update(0.25f);
    CHECK(animator.GetLocalPose()[0].translation.x == Catch::Approx(3));
    animator.Stop();
    scene.Update(0.25f);
    CHECK(animator.GetLocalPose()[0].translation.x == Catch::Approx(3));
}

TEST_CASE("Animator crossfades advance the outgoing clip and interrupted fades stay continuous",
          "[animation][animator]") {
    Scene scene;
    auto& animator = scene.CreateEntity().AddComponent<Animator>();
    animator.SetSkeleton(MakeSkeleton());
    REQUIRE(animator.Play(MakeClip(4), false));
    animator.Simulate(0.25f);
    REQUIRE(animator.CrossFade(MakeClip(8), 1, false));
    animator.Simulate(0.5f);
    CHECK(animator.GetLocalPose()[0].translation.x == Catch::Approx(3.5f));
    REQUIRE(animator.CrossFade(MakeClip(2), 0.5f, false));
    CHECK(animator.GetLocalPose()[0].translation.x == Catch::Approx(3.5f));
    animator.Simulate(0.5f);
    CHECK(animator.GetLocalPose()[0].translation.x == Catch::Approx(1));
    auto bad = std::make_shared<AnimationClip>(*MakeClip(2));
    bad->channels[0].bone = 4;
    CHECK_FALSE(animator.Play(bad));
    CHECK(animator.GetLocalPose()[0].translation.x == Catch::Approx(1));
}

TEST_CASE("Model instances skip joints and connect skin renderers to an animator", "[animation][model]") {
    const auto skeleton =
        std::make_shared<const Skeleton>(Skeleton{"Rig",
                                                  {{"Root", -1, {}}, {"Hand", 0, {{0, 1, 0}}}, {"Body", 0, {}}},
                                                  {0, 1},
                                                  {Matrix4{}, Matrix4::Translation({0, -1, 0})}});
    const AssetRef<Mesh> mesh(AssetId::Generate(), nullptr);
    Model model({{.name = "Root", .joint = true},
                 {.name = "Hand", .transform = Matrix4::Translation({0, 1, 0}), .parent = 0, .joint = true},
                 {.name = "Body",
                  .parent = 0,
                  .mesh = mesh,
                  .materials = {AssetRef<Material>{}},
                  .skeleton = skeleton,
                  .animations = {MakeClip(2)}}});
    Scene scene;
    auto& root = scene.Instantiate(model, "Robot");
    CHECK(scene.GetEntityCount() == 2);
    REQUIRE(root.GetComponent<Animator>());
    REQUIRE(scene.FindEntity("Body"));
    auto* renderer = scene.FindEntity("Body")->GetComponent<SkinnedMeshRenderer>();
    REQUIRE(renderer);
    CHECK(renderer->GetAnimator() == root.GetId());
    CHECK(renderer->GetPalette().size() == 2);
    scene.Update(0.5f);
    CHECK(renderer->GetPalette()[0].columns[3].x == Catch::Approx(1));
    const auto authored = scene.SaveToString();
    root.GetComponent<Animator>()->Seek(0.1f);
    CHECK(scene.SaveToString() == authored);
}
