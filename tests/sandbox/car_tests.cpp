#include "car.h"
#include "car_driver.h"
#include "hud.h"
#include "player_controller.h"
#include "player_health.h"
#include "playground_fixture.h"
#include "view_model.h"
#include "weapon_holder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using SandboxTests::Playground;

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

struct Driving {
    Playground& game;
    Car& car = *game.scene.FindComponent<Car>();
    CarDriver& driver = *game.scene.FindComponent<CarDriver>();
    PlayerController& player = *game.scene.FindComponent<PlayerController>();

    void Press(Spot::Key key) { game.input.OnEvent(Spot::KeyPressedEvent{key, false}); }
    void Release(Spot::Key key) { game.input.OnEvent(Spot::KeyReleasedEvent{key}); }
    void Tap(Spot::Key key) {
        Press(key);
        game.Step();
        Release(key);
        game.Step();
    }
    void Run(float seconds) { game.Run(static_cast<int>(seconds * 60.0f)); }
    Spot::Transform& GetCarTransform() const { return car.GetEntity().GetTransform(); }
    Spot::Vector3 GetForward() const { return GetCarTransform().GetWorldRotation() * Spot::Vector3{0.0f, 0.0f, -1.0f}; }
    // Puts the car at rest on the road at the distance east of the plaza, heading east along it.
    void PlaceOnRoad(float x) {
        GetCarTransform().SetPosition({x, 0.02f, 26.0f});
        GetCarTransform().SetRotation(Spot::Quaternion::FromAxisAngle(Up, -Spot::Pi * 0.5f));
        auto& body = *car.GetEntity().GetComponent<Spot::Rigidbody>();
        body.SetLinearVelocity({});
        body.SetAngularVelocity({});
        Run(1.0f);
    }
    // Stands the player beside the car, looking at its middle.
    void LookAtCar() {
        const Spot::Vector3 middle = GetCarTransform().GetWorldPosition() + Up * 0.8f;
        const Spot::Vector3 right = GetCarTransform().GetWorldRotation() * Spot::Vector3{1.0f, 0.0f, 0.0f};
        player.GetEntity().GetTransform().SetPosition(middle + right * 2.6f + Up * 0.3f);
        Run(0.5f);
        const Spot::Vector3 look = middle - player.GetEyePosition();
        player.SetLook(std::atan2(-look.x, -look.z), std::asin(look.y / Spot::Length(look)));
        game.Step();
    }
    const Spot::Entity* GetMainCameraEntity() const {
        const Spot::Camera* camera = Spot::FindMainCamera(game.scene);
        return camera ? &camera->GetEntity() : nullptr;
    }
};

} // namespace

TEST_CASE("The parked car rests on its springs at its ride height and stays where it is", "[sandbox][car]") {
    Playground game;
    Driving driving{game};
    const Spot::Vector3 start = driving.GetCarTransform().GetWorldPosition();
    driving.Run(3.0f);
    const Spot::Vector3 rest = driving.GetCarTransform().GetWorldPosition();
    CHECK(std::abs(rest.y - start.y) < 0.03f);
    CHECK(Spot::Length(Spot::Vector3{rest.x - start.x, 0.0f, rest.z - start.z}) < 0.02f);
    for (int wheel = 0; wheel < 4; ++wheel) {
        CHECK(driving.car.IsWheelGrounded(wheel));
    }
    CHECK_FALSE(driving.car.HasDriver());
    // Parked and still, it sleeps.
    CHECK(driving.car.GetEntity().GetComponent<Spot::Rigidbody>()->IsSleeping());
}

TEST_CASE("Use gets into the car looked at, its camera takes over, and Use again gets out on foot", "[sandbox][car]") {
    Playground game;
    Driving driving{game};
    driving.LookAtCar();
    REQUIRE(driving.driver.FindCar() == &driving.car);
    game.Step();
    CHECK(game.scene.FindComponent<Hud>()->GetState().prompt == "[E]  Drive");

    driving.Tap(Spot::Key::E);
    REQUIRE(driving.driver.IsDriving());
    CHECK(driving.car.GetDriver() == driving.player.GetEntity().GetId());
    // Seen from behind the car, in the third person, with nothing in hand.
    const Spot::Entity* camera = driving.GetMainCameraEntity();
    REQUIRE(camera);
    CHECK(camera->GetId() == driving.car.camera);
    const Spot::Vector3 fromCar =
        camera->GetTransform().GetWorldPosition() - driving.GetCarTransform().GetWorldPosition();
    CHECK(Spot::Dot(fromCar, driving.GetForward()) < -3.0f);
    CHECK_FALSE(driving.player.GetEntity().GetComponent<Spot::CharacterController>()->IsEnabled());
    CHECK_FALSE(driving.player.GetEntity().GetComponent<WeaponHolder>()->CanUseItem());
    CHECK_FALSE(driving.player.GetEntity().GetComponent<ViewModel>()->GetShownModel());
    CHECK(Spot::Distance(driving.player.GetEntity().GetTransform().GetWorldPosition(), driving.car.GetSeatPosition()) <
          0.01f);
    CHECK(game.scene.FindComponent<Hud>()->GetState().count == "0");

    driving.Tap(Spot::Key::E);
    REQUIRE_FALSE(driving.driver.IsDriving());
    CHECK_FALSE(driving.car.HasDriver());
    camera = driving.GetMainCameraEntity();
    REQUIRE(camera);
    CHECK(camera->GetId() == driving.player.head);
    CHECK(driving.player.GetEntity().GetComponent<Spot::CharacterController>()->IsEnabled());
    // Beside the driver's door, on the left, looking the way the car faces.
    const Spot::Vector3 right = driving.GetCarTransform().GetWorldRotation() * Spot::Vector3{1.0f, 0.0f, 0.0f};
    const Spot::Vector3 out =
        driving.player.GetEntity().GetTransform().GetWorldPosition() - driving.GetCarTransform().GetWorldPosition();
    CHECK(Spot::Dot(out, right) < -1.0f);
    CHECK(Spot::Length(out) < 3.0f);
    CHECK(Spot::Dot(driving.player.GetAimDirection(), driving.GetForward()) > 0.95f);
    driving.Run(0.5f);
    CHECK(driving.player.GetEntity().GetComponent<WeaponHolder>()->CanUseItem());
}

TEST_CASE("The car accelerates through its gears, brakes to a stop and reverses", "[sandbox][car]") {
    Playground game;
    Driving driving{game};
    driving.PlaceOnRoad(-55.0f);
    driving.driver.Enter(driving.car);
    const Spot::Vector3 start = driving.GetCarTransform().GetWorldPosition();

    driving.Press(Spot::Key::W);
    driving.Run(5.0f);
    driving.Release(Spot::Key::W);
    const float speed = driving.car.GetSpeed();
    CAPTURE(speed, driving.car.GetGear(), driving.car.GetRpm());
    // A brisk hatchback reaches about 85 km/h in five seconds, spinning its rear wheels at first.
    CHECK(speed > 19.0f);
    CHECK(speed < 28.0f);
    CHECK(driving.car.GetGear() >= 2);
    CHECK(Spot::Dot(driving.GetCarTransform().GetWorldPosition() - start, driving.GetForward()) > 30.0f);
    // Straight ahead, upright.
    CHECK(std::abs(driving.GetCarTransform().GetWorldPosition().z - 26.0f) < 0.5f);
    CHECK(Spot::Dot(driving.GetCarTransform().GetWorldRotation() * Up, Up) > 0.99f);

    driving.Press(Spot::Key::S);
    float stopped = -1.0f;
    for (int frame = 0; frame < 6 * 60 && stopped < 0.0f; ++frame) {
        game.Step();
        stopped = driving.car.GetSpeed() < 0.3f ? static_cast<float>(frame) / 60.0f : -1.0f;
    }
    CAPTURE(stopped);
    // Braking at most about 1 g, as tires on asphalt allow.
    REQUIRE(stopped > speed / 11.0f);
    CHECK(stopped < speed / 6.0f);
    // Held on, back reverses.
    driving.Run(2.5f);
    CHECK(driving.car.GetGear() == -1);
    CHECK(driving.car.GetSpeed() < -2.0f);
    driving.Release(Spot::Key::S);
}

TEST_CASE("Steering turns the car the way it is steered, and the handbrake locks its rear wheels", "[sandbox][car]") {
    Playground game;
    Driving driving{game};
    driving.PlaceOnRoad(-20.0f);
    driving.driver.Enter(driving.car);
    driving.Press(Spot::Key::W);
    driving.Run(2.0f);
    const Spot::Vector3 before = driving.GetForward();
    driving.Press(Spot::Key::A);
    driving.Run(0.8f);
    driving.Release(Spot::Key::A);
    driving.Release(Spot::Key::W);
    const Spot::Vector3 after = driving.GetForward();
    // Turned left, from heading east toward the north and the plaza.
    CHECK(Spot::Cross(before, after).y > 0.2f);
    CHECK(driving.car.GetSteering() < -0.5f);
    CHECK(Spot::Dot(driving.GetCarTransform().GetWorldRotation() * Up, Up) > 0.95f);

    driving.Run(0.5f);
    driving.Press(Spot::Key::Space);
    driving.Run(0.4f);
    CHECK(driving.car.GetWheelSpin(2) == Catch::Approx(0.0f).margin(0.5f));
    CHECK(driving.car.GetWheelSlip(2) > 1.0f);
    driving.Release(Spot::Key::Space);
}

TEST_CASE("Dying in the car gets the player out", "[sandbox][car]") {
    Playground game;
    Driving driving{game};
    driving.driver.Enter(driving.car);
    REQUIRE(driving.driver.IsDriving());
    game.scene.FindComponent<PlayerHealth>()->Damage(1000.0f);
    CHECK_FALSE(driving.driver.IsDriving());
    CHECK_FALSE(driving.car.HasDriver());
    game.Step();
    const Spot::Entity* camera = driving.GetMainCameraEntity();
    REQUIRE(camera);
    CHECK(camera->GetId() == driving.player.head);
}
