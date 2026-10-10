#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using Spot::GamepadAxis;
using Spot::GamepadButton;
using Spot::InputActionType;
using Spot::InputBinding;
using Spot::InputControl;
using Spot::Key;

namespace {

constexpr auto Pad = static_cast<Spot::GamepadId>(7);

Spot::InputActionMap MakeMap() {
    Spot::InputActionMap map;
    map.Add("Jump", InputActionType::Button)
        .Bind(InputControl::Of(Key::Space))
        .Bind(InputControl::Of(GamepadButton::South));
    InputBinding stick = InputBinding::Of({Spot::InputSource::GamepadStick, 0});
    stick.deadZone = 0.2f;
    map.Add("Move", InputActionType::Vector2)
        .Bind(InputBinding::Composite(InputControl::Of(Key::W), InputControl::Of(Key::S), InputControl::Of(Key::A),
                                      InputControl::Of(Key::D)))
        .Bind(stick);
    InputBinding mouse = InputBinding::Of({Spot::InputSource::MouseMotion, 0});
    mouse.scale = 0.1f;
    map.Add("Look", InputActionType::Vector2).Bind(mouse);
    map.Add("Fire", InputActionType::Button)
        .Bind(InputControl::Of(Spot::MouseButton::Left))
        .Bind(InputControl::Of(GamepadAxis::RightTrigger));
    map.Add("Throttle", InputActionType::Axis)
        .Bind(InputBinding::Composite(InputControl::Of(Key::E), InputControl::Of(Key::Q)));
    return map;
}

} // namespace

TEST_CASE("A button action is down while any of its bindings is, with edges on the frames it changed",
          "[input][actions]") {
    Spot::InputState state;
    Spot::InputActionMap map = MakeMap();

    state.BeginFrame();
    state.OnEvent(Spot::KeyPressedEvent{Key::Space, false});
    map.Update(state);
    CHECK(map.IsDown("Jump"));
    CHECK(map.IsPressed("Jump"));
    CHECK(map.GetValue("Jump") == 1.0f);

    // A second control held at once is no second press.
    state.BeginFrame();
    state.OnEvent(Spot::GamepadButtonPressedEvent{.gamepad = Pad, .button = GamepadButton::South});
    map.Update(state);
    CHECK(map.IsDown("Jump"));
    CHECK_FALSE(map.IsPressed("Jump"));

    state.BeginFrame();
    state.OnEvent(Spot::KeyReleasedEvent{Key::Space});
    map.Update(state);
    CHECK(map.IsDown("Jump"));
    CHECK_FALSE(map.IsReleased("Jump"));

    state.BeginFrame();
    state.OnEvent(Spot::GamepadButtonReleasedEvent{.gamepad = Pad, .button = GamepadButton::South});
    map.Update(state);
    CHECK_FALSE(map.IsDown("Jump"));
    CHECK(map.IsReleased("Jump"));

    state.BeginFrame();
    map.Update(state);
    CHECK_FALSE(map.IsReleased("Jump"));
}

TEST_CASE("A press and a release within one frame are both seen", "[input][actions]") {
    Spot::InputState state;
    Spot::InputActionMap map = MakeMap();

    state.BeginFrame();
    state.OnEvent(Spot::KeyPressedEvent{Key::Space, false});
    state.OnEvent(Spot::KeyReleasedEvent{Key::Space});
    map.Update(state);
    CHECK_FALSE(map.IsDown("Jump"));
    CHECK(map.IsPressed("Jump"));
    CHECK(map.IsReleased("Jump"));
}

TEST_CASE("Four buttons make a vector no longer than 1", "[input][actions]") {
    Spot::InputState state;
    Spot::InputActionMap map = MakeMap();

    state.OnEvent(Spot::KeyPressedEvent{Key::W, false});
    map.Update(state);
    CHECK(map.GetVector2("Move").x == 0.0f);
    CHECK(map.GetVector2("Move").y == 1.0f);

    state.OnEvent(Spot::KeyPressedEvent{Key::A, false});
    map.Update(state);
    CHECK(map.GetVector2("Move").x == Approx(-0.7071f).margin(0.001f));
    CHECK(map.GetVector2("Move").y == Approx(0.7071f).margin(0.001f));
    CHECK(map.IsDown("Move"));

    // Opposite buttons cancel out.
    state.OnEvent(Spot::KeyPressedEvent{Key::S, false});
    map.Update(state);
    CHECK(map.GetVector2("Move").x == -1.0f);
    CHECK(map.GetVector2("Move").y == 0.0f);
}

TEST_CASE("A stick within its dead zone counts as at rest, and past it fills the range again", "[input][actions]") {
    Spot::InputState state;
    Spot::InputActionMap map = MakeMap();

    state.OnEvent(Spot::GamepadAxisEvent{.gamepad = Pad, .axis = GamepadAxis::LeftX, .value = 0.15f});
    map.Update(state);
    CHECK(map.GetVector2("Move").x == 0.0f);
    CHECK_FALSE(map.IsDown("Move"));

    state.OnEvent(Spot::GamepadAxisEvent{.gamepad = Pad, .axis = GamepadAxis::LeftX, .value = 0.6f});
    map.Update(state);
    CHECK(map.GetVector2("Move").x == Approx(0.5f));
    CHECK(map.GetVector2("Move").y == 0.0f);

    state.OnEvent(Spot::GamepadAxisEvent{.gamepad = Pad, .axis = GamepadAxis::LeftX, .value = 1.0f});
    map.Update(state);
    CHECK(map.GetVector2("Move").x == Approx(1.0f));

    // The keys win while they go farther.
    state.OnEvent(Spot::GamepadAxisEvent{.gamepad = Pad, .axis = GamepadAxis::LeftX, .value = 0.6f});
    state.OnEvent(Spot::KeyPressedEvent{Key::S, false});
    map.Update(state);
    CHECK(map.GetVector2("Move").x == 0.0f);
    CHECK(map.GetVector2("Move").y == -1.0f);
}

TEST_CASE("Mouse motion is scaled, and up is positive", "[input][actions]") {
    Spot::InputState state;
    Spot::InputActionMap map = MakeMap();

    state.BeginFrame();
    state.OnEvent(Spot::MouseMovedEvent{.position = {}, .delta = {10.0f, 20.0f}});
    map.Update(state);
    CHECK(map.GetVector2("Look").x == Approx(1.0f));
    CHECK(map.GetVector2("Look").y == Approx(-2.0f));

    state.BeginFrame();
    map.Update(state);
    CHECK(map.GetVector2("Look").x == 0.0f);
    CHECK(map.GetVector2("Look").y == 0.0f);
}

TEST_CASE("An axis bound to a button action presses it past the press point", "[input][actions]") {
    Spot::InputState state;
    Spot::InputActionMap map = MakeMap();

    state.BeginFrame();
    state.OnEvent(Spot::GamepadAxisEvent{.gamepad = Pad, .axis = GamepadAxis::RightTrigger, .value = 0.3f});
    map.Update(state);
    CHECK_FALSE(map.IsDown("Fire"));
    CHECK(map.GetValue("Fire") == Approx(0.3f));

    state.BeginFrame();
    state.OnEvent(Spot::GamepadAxisEvent{.gamepad = Pad, .axis = GamepadAxis::RightTrigger, .value = 0.8f});
    map.Update(state);
    CHECK(map.IsDown("Fire"));
    CHECK(map.IsPressed("Fire"));

    state.BeginFrame();
    state.OnEvent(Spot::GamepadAxisEvent{.gamepad = Pad, .axis = GamepadAxis::RightTrigger, .value = 0.1f});
    map.Update(state);
    CHECK(map.IsReleased("Fire"));
}

TEST_CASE("Two buttons make an axis", "[input][actions]") {
    Spot::InputState state;
    Spot::InputActionMap map = MakeMap();

    state.OnEvent(Spot::KeyPressedEvent{Key::Q, false});
    map.Update(state);
    CHECK(map.GetValue("Throttle") == -1.0f);
    state.OnEvent(Spot::KeyPressedEvent{Key::E, false});
    map.Update(state);
    CHECK(map.GetValue("Throttle") == 0.0f);
}

TEST_CASE("A map can read one gamepad only", "[input][actions]") {
    constexpr auto Other = static_cast<Spot::GamepadId>(9);
    Spot::InputState state;
    state.OnEvent(Spot::GamepadConnectedEvent{.gamepad = Pad, .name = "One"});
    state.OnEvent(Spot::GamepadConnectedEvent{.gamepad = Other, .name = "Two"});
    Spot::InputActionMap map = MakeMap();
    map.SetGamepad(0);

    state.OnEvent(Spot::GamepadButtonPressedEvent{.gamepad = Other, .button = GamepadButton::South});
    map.Update(state);
    CHECK_FALSE(map.IsDown("Jump"));

    state.OnEvent(Spot::GamepadButtonPressedEvent{.gamepad = Pad, .button = GamepadButton::South});
    map.Update(state);
    CHECK(map.IsDown("Jump"));
}

TEST_CASE("Actions a map does not have report nothing, and maps can change", "[input][actions]") {
    Spot::InputState state;
    Spot::InputActionMap map = MakeMap();
    state.OnEvent(Spot::KeyPressedEvent{Key::Space, false});
    map.Update(state);

    CHECK_FALSE(map.IsDown("Crouch"));
    CHECK(map.GetValue("Crouch") == 0.0f);
    CHECK(map.GetVector2("Crouch").x == 0.0f);

    // Adding an action of a name it has changes that one.
    map.Add("Jump", InputActionType::Button).Bind(InputControl::Of(Key::J));
    REQUIRE(map.Find("Jump"));
    CHECK(map.Find("Jump")->bindings.size() == 3);
    CHECK(map.GetActions().size() == 5);

    CHECK(map.Remove("Jump"));
    CHECK_FALSE(map.Remove("Jump"));
    map.Update(state);
    CHECK_FALSE(map.IsDown("Jump"));
    CHECK(map.IsDown("Move") == false);
}

TEST_CASE("Controls have names players can read", "[input][actions]") {
    CHECK(InputControl::Of(Key::W).GetName() == "W");
    CHECK(InputControl::Of(GamepadButton::South).GetName() == "Gamepad South");
    CHECK(InputControl{Spot::InputSource::GamepadStick, 1}.GetName() == "Gamepad Right Stick");
}

TEST_CASE("Input's action queries read the current map", "[input][actions]") {
    Spot::InputState state;
    Spot::InputActionMap map = MakeMap();
    Spot::Input::SetState(&state);
    Spot::Input::SetActionMap(&map);

    state.OnEvent(Spot::KeyPressedEvent{Key::Space, false});
    state.OnEvent(Spot::KeyPressedEvent{Key::D, false});
    Spot::Input::UpdateActions();
    CHECK(Spot::Input::IsActionDown("Jump"));
    CHECK(Spot::Input::IsActionPressed("Jump"));
    CHECK_FALSE(Spot::Input::IsActionReleased("Jump"));
    CHECK(Spot::Input::GetActionVector2("Move").x == 1.0f);
    CHECK(Spot::Input::GetActionValue("Jump") == 1.0f);

    Spot::Input::SetActionMap(nullptr);
    Spot::Input::SetState(nullptr);
    CHECK_FALSE(Spot::Input::IsActionDown("Jump"));
    CHECK(Spot::Input::GetActionValue("Jump") == 0.0f);
}
