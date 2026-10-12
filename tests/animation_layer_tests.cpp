#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <spot.h>

#include <array>

using namespace Spot;

TEST_CASE("Bone masks select descendants and later entries override weights", "[animation][layers]") {
    Skeleton skeleton{"Rig", {{"Root", -1, {}}, {"Spine", 0, {}}, {"Hand", 1, {}}, {"Leg", 0, {}}}, {}, {}};
    std::array<float, 4> weights{};
    const std::array entries{BoneMaskEntry{"Spine", 0.75f, true}, BoneMaskEntry{"Hand", 0.25f, false}};
    REQUIRE(BuildBoneMask(skeleton, entries, weights));
    CHECK(weights == std::array<float, 4>{0, 0.75f, 0.25f, 0});
    REQUIRE(BuildBoneMask(skeleton, {}, weights));
    CHECK(weights == std::array<float, 4>{1, 1, 1, 1});
    const std::array invalid{BoneMaskEntry{"Missing"}};
    CHECK_FALSE(BuildBoneMask(skeleton, invalid, weights));
}

TEST_CASE("Additive layers use bind-relative translation, rotation and scale", "[animation][layers]") {
    const auto bindRotation = Quaternion::FromAxisAngle({0, 1, 0}, 0.5f);
    const auto offset = Quaternion::FromAxisAngle({1, 0, 0}, 0.8f);
    const std::array reference{TransformComponents{{1, 0, 0}, bindRotation, {2, 2, 2}}};
    const std::array base{TransformComponents{{3, 0, 0}, bindRotation, {3, 3, 3}}};
    const std::array layer{TransformComponents{{5, 0, 0}, bindRotation * offset, {4, 4, 4}}};
    const std::array mask{0.5f};
    std::array<TransformComponents, 1> result{};
    ApplyAnimationLayer(base, layer, reference, mask, 1, AnimationLayerMode::Additive, result);
    CHECK(result[0].translation.x == Catch::Approx(5));
    CHECK(result[0].scale.x == Catch::Approx(4.5f));
    const auto expected = bindRotation * Quaternion::FromAxisAngle({1, 0, 0}, 0.4f);
    CHECK(result[0].rotation.x == Catch::Approx(expected.x));
    CHECK(result[0].rotation.w == Catch::Approx(expected.w));
    ApplyAnimationLayer(base, layer, reference, mask, 1, AnimationLayerMode::Override, result);
    CHECK(result[0].translation.x == Catch::Approx(4));
    CHECK(result[0].scale.x == Catch::Approx(3.5f));
}

TEST_CASE("Animator layers keep locomotion on unmasked bones and reuse pose storage", "[animation][layers]") {
    const auto skeleton = std::make_shared<const Skeleton>(
        Skeleton{"Rig", {{"Root", -1, {}}, {"Spine", 0, {}}, {"Hand", 1, {}}, {"Leg", 0, {}}}, {}, {}});
    const auto base = std::make_shared<const AnimationClip>(AnimationClip{
        "Walk", 1, {{3, AnimationPath::Translation, AnimationInterpolation::Linear, {0, 1}, {{}, {2, 0, 0, 0}}}}});
    const auto upper = std::make_shared<const AnimationClip>(AnimationClip{
        "Aim", 1, {{1, AnimationPath::Translation, AnimationInterpolation::Linear, {0, 1}, {{}, {0, 2, 0, 0}}}}});
    Scene scene;
    auto& animator = scene.CreateEntity().AddComponent<Animator>();
    animator.SetSkeleton(skeleton);
    REQUIRE(animator.Play(base));
    const std::array layers{AnimationLayer{"Upper body", upper, AnimationLayerMode::Override, 1, 1, true, {{"Spine"}}}};
    REQUIRE(animator.SetLayers(layers));
    const auto* storage = animator.GetLocalPose().data();
    scene.Update(0.5f);
    CHECK(animator.GetLocalPose()[1].translation.y == Catch::Approx(1));
    CHECK(animator.GetLocalPose()[3].translation.x == Catch::Approx(1));
    CHECK(animator.GetLocalPose().data() == storage);
    REQUIRE(animator.SetLayerWeight("Upper body", 0));
    animator.Seek(0.5f);
    CHECK(animator.GetLocalPose()[1].translation.y == 0);
    CHECK(animator.GetLocalPose()[3].translation.x == Catch::Approx(1));
    auto bad = layers;
    bad[0].mask = {{"Missing"}};
    CHECK_FALSE(animator.SetLayers(bad));
    CHECK(animator.GetLayers()[0].name == "Upper body");
}
