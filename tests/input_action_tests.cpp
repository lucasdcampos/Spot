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

    // And a release and a press of a key held.
    state.OnEvent(Spot::KeyPressedEvent{Key::Space, false});
    map.Update(state);
    state.BeginFrame();
    state.OnEvent(Spot::KeyReleasedEvent{Key::Space});
    state.OnEvent(Spot::KeyPressedEvent{Key::Space, false});
    map.Update(state);
    CHECK(map.IsDown("Jump"));
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

TEST_CASE("The input settings section keeps the actions and their bindings", "[input][actions][settings]") {
    Spot::SettingsSections sections;
    sections.input = Spot::InputSettings::MakeDefault();
    sections.input.actions[0].bindings[1].invertY = true;
    sections.input.actions[2].pressPoint = 0.75f;

    Spot::SettingsSections loaded;
    REQUIRE(loaded.LoadFromString(sections.SaveToString()));
    REQUIRE(loaded.input.actions.size() == sections.input.actions.size());
    for (std::size_t i = 0; i < loaded.input.actions.size(); ++i) {
        const Spot::InputAction& action = loaded.input.actions[i];
        const Spot::InputAction& saved = sections.input.actions[i];
        CHECK(action.name == saved.name);
        CHECK(action.type == saved.type);
        CHECK(action.pressPoint == saved.pressPoint);
        REQUIRE(action.bindings.size() == saved.bindings.size());
        for (std::size_t j = 0; j < action.bindings.size(); ++j) {
            CHECK(action.bindings[j].control == saved.bindings[j].control);
            CHECK(action.bindings[j].parts == saved.bindings[j].parts);
            CHECK(action.bindings[j].deadZone == saved.bindings[j].deadZone);
            CHECK(action.bindings[j].scale == saved.bindings[j].scale);
            CHECK(action.bindings[j].invertX == saved.bindings[j].invertX);
            CHECK(action.bindings[j].invertY == saved.bindings[j].invertY);
        }
    }

    // Games of older projects have no actions.
    CHECK(Spot::SettingsSections{}.input.actions.empty());
}

TEST_CASE("The default actions are Move, Look, Jump, Fire and Pause", "[input][actions][settings]") {
    const Spot::InputSettings settings = Spot::InputSettings::MakeDefault();
    Spot::InputActionMap map;
    map.Add("Jump", InputActionType::Axis);
    map.Add("Custom", InputActionType::Button);
    settings.Apply(map);

    for (const char* name : {"Move", "Look", "Jump", "Fire", "Pause"}) {
        CHECK(map.Find(name) != nullptr);
    }
    // Actions of the same names are replaced, and others kept.
    CHECK(map.Find("Jump")->type == InputActionType::Button);
    CHECK(map.Find("Custom") != nullptr);

    Spot::InputState state;
    state.OnEvent(Spot::GamepadButtonPressedEvent{.gamepad = Pad, .button = GamepadButton::Start});
    map.Update(state);
    CHECK(map.IsPressed("Pause"));
}

TEST_CASE("The control pressed this frame is found, buttons before axes", "[input][actions]") {
    Spot::InputState state;
    state.BeginFrame();
    CHECK_FALSE(Spot::FindPressedControl(state).has_value());

    state.OnEvent(Spot::GamepadAxisEvent{.gamepad = Pad, .axis = GamepadAxis::LeftTrigger, .value = 0.9f});
    CHECK(Spot::FindPressedControl(state) == InputControl::Of(GamepadAxis::LeftTrigger));
    state.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Right});
    CHECK(Spot::FindPressedControl(state) == InputControl::Of(Spot::MouseButton::Right));
    state.OnEvent(Spot::KeyPressedEvent{Key::F, false});
    CHECK(Spot::FindPressedControl(state) == InputControl::Of(Key::F));

    // Held keys were pressed before.
    state.BeginFrame();
    state.OnEvent(Spot::GamepadAxisEvent{.gamepad = Pad, .axis = GamepadAxis::LeftTrigger, .value = 0.0f});
    CHECK_FALSE(Spot::FindPressedControl(state).has_value());
}

TEST_CASE("Players rebind actions, and reset them to their defaults", "[input][actions][rebind]") {
    Spot::InputActionMap map = MakeMap();
    map.SetDefaults();
    CHECK_FALSE(map.IsRebound("Jump"));

    REQUIRE(map.Rebind("Jump", 0, InputControl::Of(Key::J)));
    REQUIRE(map.Rebind("Move", 0, InputControl::Of(Key::Up), 0));
    CHECK_FALSE(map.Rebind("Jump", 5, InputControl::Of(Key::J)));
    CHECK_FALSE(map.Rebind("Crouch", 0, InputControl::Of(Key::J)));
    CHECK(map.IsRebound("Jump"));
    CHECK(map.IsRebound("Move"));
    CHECK_FALSE(map.IsRebound("Fire"));

    Spot::InputState state;
    state.OnEvent(Spot::KeyPressedEvent{Key::J, false});
    state.OnEvent(Spot::KeyPressedEvent{Key::Up, false});
    state.OnEvent(Spot::KeyPressedEvent{Key::Space, false});
    map.Update(state);
    CHECK(map.IsDown("Jump"));
    CHECK(map.GetVector2("Move").y == 1.0f);

    CHECK(map.ResetBindings("Jump"));
    CHECK_FALSE(map.IsRebound("Jump"));
    CHECK(map.IsRebound("Move"));
    map.ResetBindings();
    CHECK_FALSE(map.IsRebound("Move"));
    CHECK(map.Find("Move")->bindings[0].parts[0] == InputControl::Of(Key::W));
}

TEST_CASE("Rebinds are saved for the actions players changed only", "[input][actions][rebind]") {
    Spot::InputActionMap map = MakeMap();
    map.SetDefaults();
    map.Rebind("Jump", 1, InputControl::Of(GamepadButton::North));
    const std::string saved = map.SaveRebindsToString();

    // A game update changed Fire's defaults, which the player never rebound.
    Spot::InputActionMap updated = MakeMap();
    updated.Find("Fire")->bindings[0].control = InputControl::Of(Spot::MouseButton::Middle);
    updated.SetDefaults();
    REQUIRE(updated.LoadRebindsFromString(saved));
    CHECK(updated.Find("Jump")->bindings[1].control == InputControl::Of(GamepadButton::North));
    CHECK(updated.IsRebound("Jump"));
    CHECK(updated.Find("Fire")->bindings[0].control == InputControl::Of(Spot::MouseButton::Middle));

    // Rebinds of actions a map does not have are ignored, and text that holds none changes nothing.
    Spot::InputActionMap other;
    other.Add("Crouch", InputActionType::Button).Bind(InputControl::Of(Key::C));
    other.SetDefaults();
    CHECK(other.LoadRebindsFromString(saved));
    CHECK_FALSE(other.IsRebound("Crouch"));
    CHECK_FALSE(updated.LoadRebindsFromString("[]"));
    CHECK_FALSE(updated.LoadRebindsFromString(R"({"format": "something-else"})"));
    CHECK(updated.IsRebound("Jump"));
}

TEST_CASE("Mouse and wheel bindings can read one axis", "[input][actions]") {
    Spot::InputActionMap map;
    InputBinding wheel = InputBinding::Of(InputControl::Scroll(Spot::InputAxes::Y));
    wheel.invertX = true;
    map.Add("Switch", InputActionType::Axis).Bind(wheel);
    map.Add("Turn", InputActionType::Vector2).Bind(InputControl::MouseMotion(Spot::InputAxes::X));
    Spot::InputState state;

    state.OnEvent(Spot::MouseScrolledEvent{.delta = {0.5f, 2.0f}});
    state.OnEvent(Spot::MouseMovedEvent{.position = {}, .delta = {3.0f, 4.0f}});
    map.Update(state);
    CHECK(map.GetValue("Switch") == -2.0f);
    CHECK(map.GetVector2("Turn").x == 3.0f);
    CHECK(map.GetVector2("Turn").y == 0.0f);
    CHECK(InputControl::Scroll(Spot::InputAxes::Y).GetName() == "Mouse Wheel Y");
    CHECK(InputControl::MouseMotion().GetName() == "Mouse");
}
