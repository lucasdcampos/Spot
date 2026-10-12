#include "animation/animator_controller.h"
#include "temp_directory.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <spot.h>
#include <spot_import.h>

#include <fstream>
#include <limits>

using namespace Spot;

namespace {
AssetRef<AnimatorController> MakeController() {
    auto clip = std::make_shared<const AnimationClip>(AnimationClip{"Idle", 1, {}});
    AnimatorController controller;
    controller.parameters = {{"speed", AnimatorParameterType::Float},
                             {"mode", AnimatorParameterType::Int},
                             {"grounded", AnimatorParameterType::Bool, 1},
                             {"fire", AnimatorParameterType::Trigger}};
    controller.states = {{"Idle", clip}, {"Walk", clip}, {"Fire", clip}};
    controller.transitions = {{0, 1, {{"speed", AnimatorComparison::Greater, 0.5}}, -1, 0.2f, false},
                              {-1, 2, {{"fire"}}, -1, 0.1f, true},
                              {2, 0, {}, 1, 0.1f, true}};
    return std::make_shared<const AnimatorController>(std::move(controller));
}
} // namespace

TEST_CASE("Controller parameters are typed and triggers are consumed only by a selected transition",
          "[animation][controller]") {
    AnimatorControllerRuntime runtime;
    runtime.SetController(MakeController());
    CHECK(runtime.GetState() == 0);
    CHECK(runtime.GetParameter("grounded") == 1);
    CHECK_FALSE(runtime.SetBool("speed", true));
    CHECK_FALSE(runtime.SetFloat("missing", 1));
    CHECK_FALSE(runtime.SetFloat("speed", std::numeric_limits<float>::infinity()));
    REQUIRE(runtime.SetInt("mode", 16777217));
    CHECK(runtime.GetParameter("mode") == 16777217);
    REQUIRE(runtime.SetFloat("speed", 2));
    REQUIRE(runtime.Advance(0.1f, 1));
    CHECK(runtime.GetState() == 1);
    REQUIRE(runtime.SetTrigger("fire"));
    CHECK_FALSE(runtime.Advance(0.05f, 1));
    CHECK(runtime.GetParameter("fire") == 1);
    REQUIRE(runtime.Advance(0.2f, 1));
    CHECK(runtime.GetState() == 2);
    CHECK(runtime.GetParameter("fire") == 0);
    CHECK_FALSE(runtime.Advance(0.5f, 1));
    REQUIRE(runtime.Advance(0.5f, 1));
    CHECK(runtime.GetState() == 0);
    runtime.Restart();
    CHECK(runtime.GetParameter("speed") == 0);
    CHECK(runtime.GetStateTime() == 0);
}

TEST_CASE("Any-state transitions can interrupt a crossfade and controller instances stay independent",
          "[animation][controller]") {
    auto controller = std::make_shared<AnimatorController>(*MakeController());
    controller->transitions[0].interruptible = true;
    AnimatorControllerRuntime a, b;
    a.SetController(controller);
    b.SetController(controller);
    a.SetFloat("speed", 1);
    REQUIRE(a.Advance(0.1f, 1));
    a.SetTrigger("fire");
    REQUIRE(a.Advance(0.01f, 1));
    CHECK(a.GetState() == 2);
    CHECK(b.GetState() == 0);
    CHECK(b.GetParameter("fire") == 0);
}

TEST_CASE("Controller assets validate, cook and load clips without GPU resources", "[animation][controller][assets]") {
    TempDirectory directory;
    const auto source = directory.GetPath() / "assets";
    std::filesystem::create_directories(source);
    std::filesystem::copy_file(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "models/animation_only.gltf",
                               source / "rig.gltf");
    AssetDatabase database(source, directory.GetPath() / "imported");
    database.Refresh();
    AssetManager assets(directory.GetPath() / "imported");
    AnimatorController controller;
    controller.states = {{"Move", assets.Load<AnimationClip>("rig.gltf#Move")}};
    controller.parameters = {{"speed", AnimatorParameterType::Float}};
    REQUIRE(controller.IsValid());
    const auto text = controller.SaveToString();
    REQUIRE(AnimatorController::LoadFromString(text, &assets));
    std::ofstream(source / "robot.controller") << text;
    database.Refresh();
    assets.RefreshManifest();
    const auto loaded = assets.Load<AnimatorController>("robot.controller");
    REQUIRE(loaded);
    REQUIRE(loaded->states[0].clip);
    CHECK(loaded->states[0].clip->name == "Move");
    controller.transitions.push_back({.to = 10});
    CHECK_FALSE(controller.IsValid());
    CHECK_FALSE(AnimatorController::LoadFromString("{}"));
    CHECK_FALSE(AnimatorController::LoadFromString("not json"));
    controller.transitions.clear();
    controller.parameters.push_back(controller.parameters[0]);
    CHECK_FALSE(controller.IsValid());
}

TEST_CASE("Animator controllers change the evaluated pose through typed parameters", "[animation][controller]") {
    auto skeleton = std::make_shared<const Skeleton>(Skeleton{"Rig", {{"Root", -1, {}}}, {0}, {Matrix4{}}});
    auto idle = std::make_shared<const AnimationClip>(AnimationClip{"Idle", 1, {}});
    auto walk = std::make_shared<const AnimationClip>(AnimationClip{
        "Walk", 1, {{0, AnimationPath::Translation, AnimationInterpolation::Linear, {0, 1}, {{}, {4, 0, 0, 0}}}}});
    auto controller = std::make_shared<AnimatorController>();
    controller->parameters = {{"speed", AnimatorParameterType::Float}};
    controller->states = {{"Idle", idle}, {"Walk", walk}};
    controller->transitions = {{0, 1, {{"speed", AnimatorComparison::Greater, 0.5}}, -1, 0}};
    Scene scene;
    auto& animator = scene.CreateEntity().AddComponent<Animator>();
    animator.SetSkeleton(skeleton);
    REQUIRE(animator.SetController(controller));
    scene.Update(0.25f);
    CHECK(animator.GetLocalPose()[0].translation.x == 0);
    REQUIRE(animator.SetFloat("speed", 1));
    scene.Update(0.1f);
    CHECK(animator.GetControllerRuntime().GetState() == 1);
    scene.Update(0.25f);
    CHECK(animator.GetLocalPose()[0].translation.x == Catch::Approx(1));
}
