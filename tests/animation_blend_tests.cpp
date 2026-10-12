#include "animation/blend_tree.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <spot.h>

#include <numeric>

using namespace Spot;

namespace {
AssetRef<AnimationClip> Motion(float distance, float duration) {
    return std::make_shared<const AnimationClip>(AnimationClip{
        "Motion",
        duration,
        {{0, AnimationPath::Translation, AnimationInterpolation::Linear, {0, duration}, {{}, {distance, 0, 0, 0}}}}});
}
} // namespace

TEST_CASE("One-dimensional blend trees interpolate unsorted thresholds and clamp endpoints", "[animation][blend]") {
    BlendTree tree{BlendTreeType::OneDimensional,
                   "speed",
                   {},
                   {{Motion(8, 1), {2, 0}}, {Motion(0, 1), {0, 0}}, {Motion(4, 2), {1, 0}}}};
    REQUIRE(tree.IsValid());
    std::vector<float> weights(3);
    ComputeBlendWeights(tree, {0.5f, 0}, weights);
    CHECK(weights[0] == 0);
    CHECK(weights[1] == Catch::Approx(0.5f));
    CHECK(weights[2] == Catch::Approx(0.5f));
    ComputeBlendWeights(tree, {-2, 0}, weights);
    CHECK(weights[1] == 1);
    ComputeBlendWeights(tree, {4, 0}, weights);
    CHECK(weights[0] == 1);
    ComputeBlendWeights(tree, {1, 0}, weights);
    CHECK(weights[2] == 1);
    tree.motions.push_back(tree.motions[0]);
    CHECK_FALSE(tree.IsValid());
}

TEST_CASE("Directional blend trees blend angles and multiple speeds on one direction", "[animation][blend]") {
    const auto clip = Motion(4, 1);
    BlendTree tree{BlendTreeType::Directional2D,
                   "x",
                   "y",
                   {{clip, {}}, {clip, {1, 0}}, {clip, {2, 0}}, {clip, {0, 1}}, {clip, {-1, 0}}, {clip, {0, -1}}}};
    REQUIRE(tree.IsValid());
    std::vector<float> weights(6);
    ComputeBlendWeights(tree, {1.5f, 0}, weights);
    CHECK(weights[1] == Catch::Approx(0.5f));
    CHECK(weights[2] == Catch::Approx(0.5f));
    CHECK(weights[0] == 0);
    ComputeBlendWeights(tree, {0.7071068f, 0.7071068f}, weights);
    CHECK(weights[1] > 0);
    CHECK(weights[3] > 0);
    CHECK(weights[4] == 0);
    CHECK(weights[5] == 0);
    for (const auto input : {Vector2{}, Vector2{1, 0}, Vector2{-10, 1}, Vector2{0.1f, 0.1f}}) {
        ComputeBlendWeights(tree, input, weights);
        CHECK(std::accumulate(weights.begin(), weights.end(), 0.0f) == Catch::Approx(1));
        for (float weight : weights) {
            CHECK(weight >= 0);
        }
    }
}

TEST_CASE("Blend tree sampling synchronizes clips with different durations", "[animation][blend]") {
    const Skeleton skeleton{"Rig", {{"Root", -1, {}}}, {0}, {Matrix4{}}};
    BlendTree tree{BlendTreeType::OneDimensional, "speed", {}, {{Motion(4, 1), {0, 0}}, {Motion(8, 2), {1, 0}}}};
    std::vector<float> weights(2);
    std::vector<TransformComponents> result(1), scratch(1);
    SampleBlendTree(skeleton, tree, {0.5f, 0}, 0.75f, false, result, scratch, weights);
    CHECK(GetBlendDuration(tree, weights) == Catch::Approx(1.5f));
    CHECK(result[0].translation.x == Catch::Approx(3));
    SampleBlendTree(skeleton, tree, {0.5f, 0}, 2.25f, true, result, scratch, weights);
    CHECK(result[0].translation.x == Catch::Approx(3));
}

TEST_CASE("Controller blend states respond to live parameters", "[animation][blend]") {
    auto controller = std::make_shared<AnimatorController>();
    controller->parameters = {{"speed", AnimatorParameterType::Float}};
    controller->states = {
        {.name = "Movement",
         .blendTree = {BlendTreeType::OneDimensional, "speed", {}, {{Motion(0, 1), {0, 0}}, {Motion(4, 1), {1, 0}}}}}};
    REQUIRE(controller->IsValid());
    Scene scene;
    auto& animator = scene.CreateEntity().AddComponent<Animator>();
    animator.SetSkeleton(std::make_shared<const Skeleton>(Skeleton{"Rig", {{"Root", -1, {}}}, {0}, {Matrix4{}}}));
    REQUIRE(animator.SetController(controller));
    animator.SetFloat("speed", 0.5f);
    scene.Update(0.5f);
    CHECK(animator.GetLocalPose()[0].translation.x == Catch::Approx(1));
    animator.SetFloat("speed", 1);
    animator.Seek(0.5f);
    CHECK(animator.GetLocalPose()[0].translation.x == Catch::Approx(2));
}
