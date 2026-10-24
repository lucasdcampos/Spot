#include "registered_assets.h"
#include "temp_directory.h"
#include <spot/animation/animation_motion.h>
#include <spot/scene/animation/animation_event_listener.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <spot.h>
#include <spot_import.h>

#include <array>
#include <stdexcept>

using namespace Spot;

namespace {
AssetRef<Skeleton> Rig() {
    return std::make_shared<const Skeleton>(Skeleton{"Rig", {{"Root", -1, {}}}, {0}, {Matrix4{}}});
}
AssetRef<AnimationClip> MovingClip() {
    return std::make_shared<const AnimationClip>(
        AnimationClip{"Walk",
                      1,
                      {{0, AnimationPath::Translation, AnimationInterpolation::Linear, {0, 1}, {{}, {2, 0, 0, 0}}}},
                      {{0, "Start", {}}, {0.25f, "Step", "left"}, {0.75f, "Step", "right"}}});
}
class EventRecorder final : public Component, public AnimationEventListener {
public:
    std::vector<std::string> events;

protected:
    void OnAnimationEvent(const AnimationEvent& event) override { events.push_back(event.name + ":" + event.payload); }
};
class ThrowingEvent final : public Component, public AnimationEventListener {
public:
    int calls = 0;

protected:
    void OnAnimationEvent(const AnimationEvent&) override {
        ++calls;
        throw std::runtime_error("event failed");
    }
};
} // namespace

TEST_CASE("Animation events cross loop boundaries and reverse intervals exactly once", "[animation][events]") {
    std::array<const AnimationEvent*, 16> events{};
    const auto clip = MovingClip();
    auto count = CollectAnimationEvents(*clip, 0, 0.5f, false, events, true);
    REQUIRE(count == 2);
    CHECK(events[0]->name == "Start");
    CHECK(events[1]->payload == "left");
    count = CollectAnimationEvents(*clip, 0.5f, 1.5f, true, events);
    REQUIRE(count == 3);
    CHECK(events[0]->payload == "right");
    CHECK(events[1]->name == "Start");
    CHECK(events[2]->payload == "left");
    count = CollectAnimationEvents(*clip, 1.5f, 0.5f, true, events);
    REQUIRE(count == 3);
    CHECK(events[0]->payload == "left");
    CHECK(events[2]->payload == "right");
    CHECK(CollectAnimationEvents(*clip, 0.25f, 0.25f, true, events) == 0);
    CHECK(CollectAnimationEvents(*clip, 0, 10000, true, events) == events.size());
}

TEST_CASE("Animation callbacks fault one subscriber and seeking never dispatches events", "[animation][events]") {
    Scene scene;
    auto& entity = scene.CreateEntity();
    auto& animator = entity.AddComponent<Animator>();
    auto& failing = entity.AddComponent<ThrowingEvent>();
    auto& recorder = entity.AddComponent<EventRecorder>();
    animator.SetSkeleton(Rig());
    REQUIRE(animator.Play(MovingClip()));
    scene.Update(0.5f);
    CHECK(failing.IsFaulted());
    CHECK(failing.calls == 1);
    CHECK(recorder.events == std::vector<std::string>{"Start:", "Step:left"});
    animator.Seek(0.7f);
    animator.Preview(0.2f);
    CHECK(recorder.events.size() == 2);
    scene.Update(0.4f);
    CHECK(failing.calls == 1);
    CHECK_FALSE(animator.IsFaulted());
    CHECK(recorder.events.size() == 4);
}

TEST_CASE("Root motion accumulates translation and turning over whole cycles", "[animation][root_motion]") {
    const auto skeleton = Rig();
    const auto clip = MovingClip();
    CHECK(GetRootMotionDelta(*skeleton, *clip, 0, 0.75f, 1.25f, true).translation.x == Catch::Approx(1));
    CHECK(GetRootMotionDelta(*skeleton, *clip, 0, 1.25f, 0.75f, true).translation.x == Catch::Approx(-1));
    auto turning = *clip;
    const auto quarterTurn = Quaternion::FromAxisAngle({0, 1, 0}, Pi * 0.5f);
    turning.channels.push_back({0,
                                AnimationPath::Rotation,
                                AnimationInterpolation::Linear,
                                {0, 1},
                                {{0, 0, 0, 1}, {quarterTurn.x, quarterTurn.y, quarterTurn.z, quarterTurn.w}}});
    const auto delta = GetRootMotionDelta(*skeleton, turning, 0, 0, 2, true);
    CHECK(delta.translation.x == Catch::Approx(2).margin(1e-4f));
    CHECK(delta.translation.z == Catch::Approx(-2).margin(1e-4f));
    CHECK(std::abs(delta.rotation.w) == Catch::Approx(0).margin(1e-4f));
}

TEST_CASE("Root motion moves transforms and characters while preview leaves the world still",
          "[animation][root_motion]") {
    Scene scene;
    auto& entity = scene.CreateEntity();
    auto& animator = entity.AddComponent<Animator>();
    animator.SetSkeleton(Rig());
    REQUIRE(animator.Play(MovingClip()));
    animator.SetRootMotion(RootMotionMode::Transform);
    scene.Update(1.25f);
    CHECK(entity.GetTransform().GetPosition().x == Catch::Approx(2.5f));
    CHECK(animator.GetLocalPose()[0].translation.x == 0);
    animator.Preview(0.5f);
    CHECK(entity.GetTransform().GetPosition().x == Catch::Approx(2.5f));
    CHECK_FALSE(animator.SetRootMotionBone("missing"));
    auto& characterEntity = scene.CreateEntity();
    characterEntity.GetTransform().SetPosition({0, 1, 0});
    characterEntity.AddComponent<CharacterController>();
    auto& characterAnimator = characterEntity.AddComponent<Animator>();
    characterAnimator.SetSkeleton(Rig());
    REQUIRE(characterAnimator.Play(MovingClip()));
    characterAnimator.SetRootMotion(RootMotionMode::CharacterController);
    auto& wall = scene.CreateEntity();
    wall.GetTransform().SetPosition({1.5f, 1, 0});
    wall.AddComponent<BoxCollider>(Vector3{0.2f, 4, 4});
    scene.Update(1);
    CHECK(characterEntity.GetTransform().GetPosition().x > 0.2f);
    CHECK(characterEntity.GetTransform().GetPosition().x < 1.4f);
}

TEST_CASE("Model metadata event overrides survive cooking and load with clips", "[animation][events][assets]") {
    TempDirectory directory;
    const auto source = directory.GetPath() / "assets";
    std::filesystem::create_directories(source);
    std::filesystem::copy_file(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "models/animation_only.gltf",
                               source / "rig.gltf");
    AssetDatabase database(source, directory.GetPath() / "imported");
    database.Refresh();
    const auto id = database.FindByPath("rig.gltf");
    REQUIRE(id);
    database.GetMeta(*id)->model.animations = {{"Move", {{0.25f, "Footstep", "left"}}}};
    REQUIRE(database.SaveMeta(*id));
    REQUIRE(database.Reimport(*id));
    Spot::Tests::RegisteredAssetManager assets(directory.GetPath() / "imported");
    const auto clip = assets.Load<AnimationClip>("rig.gltf#Move");
    REQUIRE(clip);
    REQUIRE(clip->events.size() == 1);
    CHECK(clip->events[0].payload == "left");
    CHECK(clip->events[0].time == 0.25f);
}
