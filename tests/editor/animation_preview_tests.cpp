#include "scene/animation_preview.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <spot.h>

TEST_CASE("Animation previews seek and pause without changing scene data or running gameplay", "[editor][animation]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    auto& animator = entity.AddComponent<Spot::Animator>();
    animator.SetSkeleton(
        std::make_shared<const Spot::Skeleton>(Spot::Skeleton{"Rig", {{"Root", -1, {}}}, {0}, {Spot::Matrix4{}}}));
    REQUIRE(animator.Play(std::make_shared<const Spot::AnimationClip>(Spot::AnimationClip{
        "Move",
        1,
        {{0, Spot::AnimationPath::Translation, Spot::AnimationInterpolation::Linear, {0, 1}, {{}, {2, 0, 0, 0}}}}})));
    const auto authored = scene.SaveToString();
    Spot::Editor::AnimationPreview preview;
    preview.Select(scene, entity.GetId());
    preview.Update(scene, 0.25f);
    CHECK(animator.GetLocalPose()[0].translation.x == Catch::Approx(0.5f));
    CHECK(scene.GetUpdateCount() == 0);
    CHECK(scene.SaveToString() == authored);
    preview.SetPlaying(false);
    preview.Update(scene, 1);
    CHECK(preview.GetTime(scene) == Catch::Approx(0.25f));
    preview.Seek(scene, 0.75f);
    CHECK(animator.GetLocalPose()[0].translation.x == Catch::Approx(1.5f));
    preview.Select(scene, entity.GetId(), {}, 1);
    CHECK(preview.GetTime(scene) == 0);
    entity.RemoveComponent(animator);
    preview.Update(scene, 1);
    CHECK(preview.GetEntityId() == Spot::EntityId::Invalid);
}

TEST_CASE("Alternate clip previews preserve the animator's authored clip", "[editor][animation]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    auto& animator = entity.AddComponent<Spot::Animator>();
    animator.SetSkeleton(
        std::make_shared<const Spot::Skeleton>(Spot::Skeleton{"Rig", {{"Root", -1, {}}}, {0}, {Spot::Matrix4{}}}));
    const Spot::AssetRef<Spot::AnimationClip> original =
        std::make_shared<const Spot::AnimationClip>(Spot::AnimationClip{"Idle", 1, {}});
    REQUIRE(animator.Play(original));
    const Spot::AssetRef<Spot::AnimationClip> alternate =
        std::make_shared<const Spot::AnimationClip>(Spot::AnimationClip{
            "Move",
            1,
            {{0, Spot::AnimationPath::Translation, Spot::AnimationInterpolation::Linear, {0, 1}, {{}, {4, 0, 0, 0}}}}});
    const auto authored = scene.SaveToString();
    Spot::Editor::AnimationPreview preview;
    preview.Select(scene, entity.GetId());
    preview.SetClip(alternate);
    preview.Seek(scene, 0.5f);
    CHECK(animator.GetLocalPose()[0].translation.x == Catch::Approx(2));
    CHECK(animator.GetClip() == original);
    CHECK(scene.SaveToString() == authored);
    preview.Clear(scene);
    CHECK(animator.GetLocalPose()[0].translation.x == 0);
}
