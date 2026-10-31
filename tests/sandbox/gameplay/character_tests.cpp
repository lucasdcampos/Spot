#include "gameplay_world.h"

#include "gameplay/camera/chase_camera.h"
#include "gameplay/camera/first_person_view.h"
#include "gameplay/character/carrier.h"
#include "gameplay/character/character.h"
#include "gameplay/character/character_movement.h"
#include "gameplay/character/character_visual.h"
#include "gameplay/character/flashlight.h"
#include "gameplay/character/footsteps.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/health/fall_damage.h"
#include "gameplay/health/health.h"
#include <spot/scene/physics/physics_world.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>

using Catch::Approx;

namespace {

struct CharacterWorld : GameplayTests::World {
    Spot::Entity& entity = scene.CreateEntity("Character");
    Gameplay::Character& character = entity.AddComponent<Gameplay::Character>();
    Spot::CharacterController& body = entity.AddComponent<Spot::CharacterController>();
    Gameplay::CharacterMovement& movement = entity.AddComponent<Gameplay::CharacterMovement>();

    CharacterWorld() {
        entity.GetTransform().SetPosition({0.0f, 1.0f, 0.0f});
        auto& eye = entity.CreateChild("Eye");
        eye.GetTransform().SetPosition({0.0f, 0.7f, 0.0f});
        eye.AddComponent<Spot::Camera>();
        character.eye = character.viewCamera = eye.GetId();
        entity.AddComponent<Gameplay::FirstPersonView>().head = eye.GetId();
        entity.AddComponent<Gameplay::Footsteps>();
        Run(0.5f);
    }
};

} // namespace

TEST_CASE("A character walks and runs from intents without a local player", "[sandbox][gameplay][character]") {
    CharacterWorld world;
    const auto start = world.entity.GetTransform().GetWorldPosition();
    int steps = 0;
    auto connection = world.movement.OnFootstep.Connect([&](float) { ++steps; });
    world.movement.SetIntent({.move = {0.0f, 1.0f}});
    world.Run(1.0f);
    const auto walked = world.entity.GetTransform().GetWorldPosition();
    CHECK(start.z - walked.z == Approx(world.movement.walkSpeed).margin(0.15f));
    CHECK(steps >= 2);

    world.movement.SetIntent({.move = {0.0f, 1.0f}, .sprint = true});
    world.Run(0.5f);
    CHECK(world.character.HasState("Sprinting"));
    const auto ran = world.entity.GetTransform().GetWorldPosition();
    CHECK(walked.z - ran.z == Approx(world.movement.runSpeed * 0.5f).margin(0.2f));

    world.character.SetState("HandsBusy", true);
    world.Run(0.5f);
    CHECK_FALSE(world.movement.IsSprinting());
    CHECK_FALSE(world.character.HasState("Sprinting"));
    CHECK(ran.z - world.entity.GetTransform().GetWorldPosition().z ==
          Approx(world.movement.walkSpeed * 0.5f).margin(0.15f));
}

TEST_CASE("An inspection camera changes presentation without changing the character's eyes or aim",
          "[sandbox][gameplay][character][thirdperson]") {
    CharacterWorld world;
    auto& cameraEntity = world.entity.CreateChild("Inspection");
    auto& camera = cameraEntity.AddComponent<Spot::Camera>();
    camera.SetEnabled(false);
    auto& chase = cameraEntity.AddComponent<Gameplay::ChaseCamera>();
    chase.followView = true;
    chase.orbit = false;
    chase.distance = 3;
    chase.lookHeight = 0.05f;
    chase.shoulderOffset = 0.65f;
    chase.followRate = 14;
    world.character.thirdPersonCamera = cameraEntity.GetId();
    auto& player = world.scene.CreateEntity("Player").AddComponent<Gameplay::PlayerController>();
    player.pawn = world.entity.GetId();
    world.Step();
    const auto before = world.character.GetViewPoint();
    REQUIRE(world.character.SetThirdPerson(true));
    world.Step();
    CHECK(Spot::FindMainCamera(world.scene) == &camera);
    CHECK(world.character.HasState("ThirdPerson"));
    CHECK(world.character.GetViewPoint().position == before.position);
    CHECK(world.character.GetViewPoint().direction == before.direction);
    CHECK(cameraEntity.GetTransform().GetWorldPosition().z > before.position.z + 1);
    CHECK(cameraEntity.GetTransform().GetWorldPosition().x > before.position.x + 0.6f);
    auto* view = world.entity.GetComponent<Gameplay::FirstPersonView>();
    view->SetLook(1.0f, 0.2f);
    world.Run(0.2f);
    const auto cameraForward = cameraEntity.GetTransform().GetWorldRotation() * Spot::Vector3{0, 0, -1};
    CHECK(Spot::Dot(Spot::Normalize(Spot::Vector3{cameraForward.x, 0, cameraForward.z}),
                    Spot::Normalize(Spot::Vector3{view->GetAimDirection().x, 0, view->GetAimDirection().z})) > 0.99f);
    view->SetLook(0, 0.45f);
    world.Step();
    CHECK((cameraEntity.GetTransform().GetWorldRotation() * Spot::Vector3{0, 0, -1}).y > 0.3f);
    view->SetLook(0, -0.45f);
    world.Step();
    CHECK((cameraEntity.GetTransform().GetWorldRotation() * Spot::Vector3{0, 0, -1}).y < -0.3f);
    const auto eyes = world.character.GetViewPoint();
    auto& physics = world.scene.GetOrAddSystem<Spot::PhysicsWorld>();
    const auto eyeHit = physics.Raycast(eyes.position, eyes.direction, 30, {.ignore = world.entity.GetId()});
    REQUIRE(eyeHit);
    const auto cameraHit = physics.Raycast(cameraEntity.GetTransform().GetWorldPosition(),
                                           cameraEntity.GetTransform().GetWorldRotation() * Spot::Vector3{0, 0, -1}, 30,
                                           {.ignore = world.entity.GetId()});
    REQUIRE(cameraHit);
    CHECK(Spot::Distance(eyeHit->point, cameraHit->point) < 0.01f);
    view->SetLook(0, 0);
    auto& wall = world.scene.CreateEntity("Wall behind the shoulder");
    wall.GetTransform().SetPosition({0, 1.5f, 1});
    wall.AddComponent<Spot::BoxCollider>(Spot::Vector3{2, 3, .2f});
    world.Step();
    CHECK(cameraEntity.GetTransform().GetWorldPosition().z < 0.8f);
    REQUIRE(world.character.SetThirdPerson(false));
    CHECK_FALSE(camera.IsEnabled());
    CHECK_FALSE(world.character.HasState("ThirdPerson"));
    CHECK(Spot::FindMainCamera(world.scene)->GetEntity().GetId() == world.character.eye);
}

TEST_CASE("Character presentation uses configurable pose layers on an unrelated rig without moving its body",
          "[sandbox][gameplay][character][character_visual]") {
    CharacterWorld world;
    auto& model = world.entity.CreateChild("Custom model");
    auto& animator = model.AddComponent<Spot::Animator>();
    animator.SetSkeleton(std::make_shared<const Spot::Skeleton>(
        Spot::Skeleton{"Other rig", {{"Joint", -1, {}}}, {0}, {Spot::Matrix4{}}}));
    const auto clip =
        std::make_shared<const Spot::AnimationClip>(Spot::AnimationClip{"Pose",
                                                                        1,
                                                                        {{0,
                                                                          Spot::AnimationPath::Rotation,
                                                                          Spot::AnimationInterpolation::Linear,
                                                                          {0, 1},
                                                                          {{0, 0, 0, 1}, {0, 0, 0, 1}}}}});
    auto controller = std::make_shared<Spot::AnimatorController>();
    controller->parameters = {{"Pace"}};
    controller->states = {{.name = "Idle", .clip = clip}};
    REQUIRE(animator.SetController(controller));
    const std::array layers{Spot::AnimationLayer{.name = "Bench", .clip = clip, .weight = 0},
                            Spot::AnimationLayer{.name = "Special seat", .clip = clip, .weight = 0}};
    REQUIRE(animator.SetLayers(layers));
    auto& visual = world.entity.AddComponent<Gameplay::CharacterVisual>();
    visual.modelRoot = model.GetId();
    visual.animator.Set(&animator);
    visual.speedParameter = "Pace";
    visual.seatedLayer = "Bench";
    visual.stateLayers = {{"Custom pose", "Special seat"}};
    visual.standingOffset = {0, 0.04f, 0};
    const auto position = world.entity.GetTransform().GetWorldPosition();
    world.entity.GetComponent<Gameplay::FirstPersonView>()->SetLook(0.8f, 0.3f);
    world.Step();
    CHECK(Spot::Distance(model.GetTransform().GetWorldRotation() * Spot::Vector3{0, 0, -1},
                         Spot::Quaternion::FromAxisAngle({0, 1, 0}, 0.8f) * Spot::Vector3{0, 0, -1}) < 0.001f);
    CHECK(world.entity.GetTransform().GetWorldPosition() == position);
    CHECK(animator.GetLayers()[0].weight == 0);
    world.character.SetState("Seated", true);
    world.character.SetState("Custom pose", true);
    world.entity.GetTransform().SetWorldRotation(Spot::Quaternion::FromEuler({0.1f, 1.1f, 0.2f}));
    world.Step();
    CHECK(animator.GetLayers()[0].weight == 1);
    CHECK(animator.GetLayers()[1].weight == 1);
    CHECK(model.GetTransform().GetPosition() == visual.seatedOffset);
    CHECK(model.GetTransform().GetRotation() == Spot::Quaternion{});
    CHECK(animator.GetControllerRuntime().GetParameter("Pace").value() == 0);
    world.character.SetState("Seated", false);
    world.character.SetState("Custom pose", false);
    world.movement.SetIntent({.move = {0, 1}});
    world.Run(0.2f);
    CHECK(animator.GetLayers()[0].weight == 0);
    CHECK(animator.GetLayers()[1].weight == 0);
    CHECK(animator.GetControllerRuntime().GetParameter("Pace").value() > 0.5f);
}

TEST_CASE("Character states suspend its body and block movement including noclip", "[sandbox][gameplay][character]") {
    CharacterWorld world;
    world.movement.SetIntent({.move = {0.0f, 1.0f}, .jump = true});
    world.character.SetState("Seated", true);
    CHECK_FALSE(world.body.IsEnabled());
    const auto seated = world.entity.GetTransform().GetWorldPosition();
    world.Run(0.25f);
    CHECK(world.entity.GetTransform().GetWorldPosition() == seated);
    world.character.SetState("Seated", false);
    CHECK(world.body.IsEnabled());

    world.movement.SetFlying(true);
    world.character.SetState("Dead", true);
    const auto dead = world.entity.GetTransform().GetWorldPosition();
    world.movement.SetIntent({.move = {0.0f, 1.0f}, .noclip = true});
    world.Run(0.25f);
    CHECK(world.movement.IsFlying());
    CHECK(world.entity.GetTransform().GetWorldPosition() == dead);
}

TEST_CASE("A character reports a landing to fall damage without the playground", "[sandbox][gameplay][character]") {
    CharacterWorld world;
    auto& health = world.entity.AddComponent<Gameplay::Health>();
    health.regenerationRate = 0.0f;
    auto& fall = world.entity.AddComponent<Gameplay::FallDamage>();
    int landings = 0;
    auto connection = world.character.OnLanded.Connect([&](float speed) {
        if (speed > fall.safeFallSpeed) {
            ++landings;
        }
    });
    world.entity.GetTransform().SetPosition({0.0f, 9.0f, 0.0f});
    world.Run(2.0f);
    CHECK(landings == 1);
    CHECK(health.GetHealth() < health.maxHealth);
}

TEST_CASE("The player controller supplies character intents and clears them on transfer",
          "[sandbox][gameplay][character]") {
    CharacterWorld world;
    world.actions.Add("Leap", Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::J));
    world.movement.controls.jump = "Leap";
    auto& player = world.scene.CreateEntity("Player").AddComponent<Gameplay::PlayerController>();
    player.pawn = world.entity.GetId();
    world.Step();
    world.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::J, false});
    world.Step();
    CHECK(world.movement.GetIntent().jump);
    world.Run(0.1f);
    CHECK(world.entity.GetTransform().GetWorldPosition().y > 1.1f);
    player.Unpossess();
    CHECK_FALSE(world.movement.GetIntent().jump);
    CHECK(world.movement.GetIntent().move == Spot::Vector2{});
}

TEST_CASE("A carrier drops on death and throws along its pawn's view", "[sandbox][gameplay][character]") {
    CharacterWorld world;
    auto& health = world.entity.AddComponent<Gameplay::Health>();
    auto& carrier = world.entity.AddComponent<Gameplay::Carrier>();
    auto& prop = world.scene.CreateEntity("Prop");
    prop.GetTransform().SetPosition({0.0f, 1.7f, -1.6f});
    prop.AddComponent<Spot::BoxCollider>(Spot::Vector3{0.3f, 0.3f, 0.3f});
    auto& rigidbody = prop.AddComponent<Spot::Rigidbody>();
    rigidbody.SetMass(5.0f);
    world.Step();
    CHECK(carrier.IsCarryable(prop, 2.0f));
    CHECK_FALSE(carrier.IsCarryable(prop, carrier.reach + 1.0f));
    carrier.PickUp(prop);
    CHECK(world.character.HasState("HandsBusy"));
    carrier.Throw();
    CHECK_FALSE(carrier.IsHolding());
    CHECK_FALSE(world.character.HasState("HandsBusy"));
    CHECK(Spot::Length(rigidbody.GetLinearVelocity()) == Approx(carrier.throwSpeed));
    carrier.PickUp(prop);
    health.TakeDamage({.amount = 1000.0f});
    CHECK_FALSE(carrier.IsHolding());
    CHECK_FALSE(world.character.HasState("HandsBusy"));
}

TEST_CASE("A flashlight works as an independent character part", "[sandbox][gameplay][character]") {
    GameplayTests::World world;
    auto& entity = world.scene.CreateEntity("Torch");
    auto& light = entity.CreateChild("Light").AddComponent<Spot::SpotLight>();
    auto& flashlight = entity.AddComponent<Gameplay::Flashlight>();
    flashlight.light = light.GetEntity().GetId();
    light.SetEnabled(false);
    world.Step();
    flashlight.Toggle();
    CHECK(flashlight.IsOn());
    flashlight.Toggle();
    CHECK_FALSE(flashlight.IsOn());
}

TEST_CASE("Resetting character motion clears a push without changing its look", "[sandbox][gameplay][character]") {
    CharacterWorld world;
    auto& view = *world.entity.GetComponent<Gameplay::FirstPersonView>();
    view.SetLook(0.7f, -0.3f);
    const auto direction = view.GetAimDirection();
    world.movement.Push({5.0f, 8.0f, 0.0f});
    world.movement.SetIntent({.jump = true});
    world.character.ResetMotion();
    const auto start = world.entity.GetTransform().GetWorldPosition();
    world.Run(0.2f);
    CHECK(view.GetAimDirection() == direction);
    CHECK(world.entity.GetTransform().GetWorldPosition().x == Approx(start.x).margin(0.01f));
    CHECK(world.entity.GetTransform().GetWorldPosition().y <= start.y + 0.05f);
}

TEST_CASE("A carrier releases immediately on a configured pawn state", "[sandbox][gameplay][character]") {
    CharacterWorld world;
    auto& carrier = world.entity.AddComponent<Gameplay::Carrier>();
    carrier.releaseWhen.push_back("Placing");
    auto& prop = world.scene.CreateEntity("Prop");
    prop.AddComponent<Spot::BoxCollider>();
    prop.AddComponent<Spot::Rigidbody>();
    world.Step();
    carrier.PickUp(prop);
    REQUIRE(carrier.IsHolding());
    world.character.SetState("Placing", true);
    CHECK_FALSE(carrier.IsHolding());
    CHECK_FALSE(world.character.HasState("HandsBusy"));
}

TEST_CASE("A local movement intent reaches the same frame's first physics step", "[sandbox][gameplay][character]") {
    CharacterWorld world;
    auto& player = world.scene.CreateEntity("Controller").AddComponent<Gameplay::PlayerController>();
    player.pawn = world.entity.GetId();
    world.actions.Add("Leap", Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::J));
    world.movement.controls.jump = "Leap";
    world.Step();
    const float height = world.entity.GetTransform().GetWorldPosition().y;
    world.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::J, false});
    world.Step();
    CHECK(world.entity.GetTransform().GetWorldPosition().y > height + 0.05f);
}

TEST_CASE("A character cannot steer after taking off", "[sandbox][gameplay][character]") {
    CharacterWorld world;
    world.movement.SetIntent({.jump = true});
    world.Run(0.1f);
    const auto before = world.entity.GetTransform().GetWorldPosition();
    world.movement.SetIntent({.move = {1.0f, 0.0f}});
    world.Run(0.1f);
    const auto after = world.entity.GetTransform().GetWorldPosition();
    CHECK(after.x == Approx(before.x).margin(0.01f));
    CHECK(after.y > before.y);
}
