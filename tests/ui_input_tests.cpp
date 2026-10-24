#include "spot/scene/ui/ui_input_system.h"
#include "spot/scene/ui/ui_system.h"
#include <spot.h>

#include <catch2/catch_test_macros.hpp>

namespace {
Spot::Entity& Button(Spot::Entity& parent, const char* name, Spot::Vector2 offset) {
    auto& entity = parent.CreateChild(name);
    auto& rect = entity.AddComponent<Spot::UiRect>();
    rect.layout.anchorMin = rect.layout.anchorMax = rect.layout.pivot = {};
    rect.layout.offset = offset;
    rect.layout.sizeDelta = {100, 40};
    entity.AddComponent<Spot::UiButton>();
    return entity;
}
} // namespace

TEST_CASE("UI pointer routing respects topmost draw order and drag cancellation", "[ui][input]") {
    Spot::InputState input;
    Spot::Scene scene;
    auto& root = scene.CreateEntity("Canvas");
    root.AddComponent<Spot::Canvas>();
    auto& back = Button(root, "Back", {10, 10});
    auto& front = Button(root, "Front", {10, 10});
    int backClicks = 0, frontClicks = 0;
    auto a = back.GetComponent<Spot::UiButton>()->OnClick.Connect([&] { ++backClicks; });
    auto b = front.GetComponent<Spot::UiButton>()->OnClick.Connect([&] { ++frontClicks; });
    auto send = [&](const Spot::Event& event) {
        return scene.GetOrAddSystem<Spot::UiInputSystem>().HandleUiEvent(event, {800, 600}, &input);
    };
    send(Spot::MouseMovedEvent{{20, 580}, {}});
    CHECK(send(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left}));
    CHECK(send(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left}));
    CHECK(frontClicks == 1);
    CHECK(backClicks == 0);
    send(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left});
    send(Spot::MouseMovedEvent{{500, 500}, {}});
    send(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left});
    CHECK(frontClicks == 1);
    front.SetActive(false);
    send(Spot::MouseMovedEvent{{20, 580}, {}});
    send(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left});
    send(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left});
    CHECK(backClicks == 1);
    CHECK_FALSE(input.IsMouseButtonPressed(Spot::MouseButton::Left));
}

TEST_CASE("Keyboard and gamepad navigate activate and reclaim focus after deletion", "[ui][input]") {
    Spot::InputState input;
    Spot::Scene scene;
    auto& root = scene.CreateEntity("Canvas");
    root.AddComponent<Spot::Canvas>();
    auto& left = Button(root, "Left", {10, 100});
    auto& right = Button(root, "Right", {200, 100});
    int clicks = 0;
    auto connection = right.GetComponent<Spot::UiButton>()->OnClick.Connect([&] { ++clicks; });
    auto send = [&](const Spot::Event& event) {
        return scene.GetOrAddSystem<Spot::UiInputSystem>().HandleUiEvent(event, {800, 600}, &input);
    };
    CHECK(send(Spot::KeyPressedEvent{Spot::Key::Tab, false}));
    auto& ui = *scene.GetSystem<Spot::UiInputSystem>();
    CHECK(ui.GetFocusedEntity() == left.GetId());
    CHECK(send(Spot::KeyPressedEvent{Spot::Key::Right, false}));
    CHECK(ui.GetFocusedEntity() == right.GetId());
    send(Spot::GamepadButtonPressedEvent{Spot::GamepadId{1}, Spot::GamepadButton::South});
    CHECK(clicks == 1);
    send(Spot::GamepadButtonPressedEvent{Spot::GamepadId{1}, Spot::GamepadButton::DPadLeft});
    CHECK(ui.GetFocusedEntity() == left.GetId());
    left.Destroy();
    ui.Refresh({800, 600});
    CHECK(ui.GetFocusedEntity() == Spot::EntityId::Invalid);
    send(Spot::KeyPressedEvent{Spot::Key::Tab, false});
    CHECK(ui.GetFocusedEntity() == right.GetId());
}

TEST_CASE("Text focus and modal canvases own input and restore the requested cursor lock", "[ui][input]") {
    Spot::InputState input;
    input.SetCursorLocked(true);
    Spot::Scene scene;
    auto& root = scene.CreateEntity("Canvas");
    auto& canvas = root.AddComponent<Spot::Canvas>();
    auto& entity = root.CreateChild("Field");
    entity.AddComponent<Spot::UiRect>();
    auto& field = entity.AddComponent<Spot::UiInputField>();
    scene.GetOrAddSystem<Spot::UiInputSystem>().HandleUiEvent(Spot::KeyPressedEvent{Spot::Key::Tab, false}, {800, 600},
                                                              &input);
    auto& ui = *scene.GetSystem<Spot::UiInputSystem>();
    CHECK(ui.WantsTextInput());
    CHECK(input.IsCaptured());
    CHECK_FALSE(input.IsCursorLockApplied());
    CHECK(scene.GetOrAddSystem<Spot::UiInputSystem>().HandleUiEvent(Spot::TextInputEvent{"Hello"}, {800, 600}, &input));
    CHECK(field.GetText() == "Hello");
    CHECK(input.GetTypedText().empty());
    ui.ClearFocus();
    CHECK_FALSE(input.IsCaptured());
    CHECK(input.IsCursorLockApplied());
    canvas.settings.modal = true;
    ui.Refresh({800, 600});
    CHECK(input.IsCaptured());
    auto cancel = ui.OnCancel.Connect([&](Spot::EntityId id) {
        CHECK(id == root.GetId());
        root.SetActive(false);
    });
    CHECK(scene.GetOrAddSystem<Spot::UiInputSystem>().HandleUiEvent(Spot::KeyPressedEvent{Spot::Key::Escape, false},
                                                                    {800, 600}, &input));
    CHECK_FALSE(input.IsCaptured());
    CHECK(input.IsCursorLockApplied());
}

TEST_CASE("Navigation reveals offscreen scroll children and clipped pointers cannot activate them", "[ui][input]") {
    Spot::InputState input;
    Spot::Scene scene;
    auto& root = scene.CreateEntity("Canvas");
    root.AddComponent<Spot::Canvas>();
    auto& panel = root.CreateChild("Scroll");
    auto& rect = panel.AddComponent<Spot::UiRect>();
    rect.layout.sizeDelta = {300, 100};
    auto& scroll = panel.AddComponent<Spot::UiScrollView>();
    scroll.contentSize = {300, 400};
    auto& button = Button(panel, "Bottom", {10, 10});
    int clicks = 0;
    auto click = button.GetComponent<Spot::UiButton>()->OnClick.Connect([&] { ++clicks; });
    scene.GetOrAddSystem<Spot::UiInputSystem>().HandleUiEvent(Spot::MouseMovedEvent{{310, 535}, {}}, {800, 600},
                                                              &input);
    scene.GetOrAddSystem<Spot::UiInputSystem>().HandleUiEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left},
                                                              {800, 600}, &input);
    scene.GetOrAddSystem<Spot::UiInputSystem>().HandleUiEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left},
                                                              {800, 600}, &input);
    CHECK(clicks == 0);
    auto& ui = *scene.GetSystem<Spot::UiInputSystem>();
    REQUIRE(ui.Focus(button.GetId()));
    CHECK(scroll.GetOffset().y > 200);
    const auto* layout = scene.GetSystem<Spot::UiSystem>()->Find(button.GetId());
    CHECK(layout->clip.Contains(layout->bounds.GetCenter()));
    scene.GetOrAddSystem<Spot::UiInputSystem>().HandleUiEvent(
        Spot::GamepadButtonPressedEvent{Spot::GamepadId{1}, Spot::GamepadButton::South}, {800, 600}, &input);
    CHECK(clicks == 1);
}

TEST_CASE("UI systems are created once across repeated events and layout passes", "[ui][input]") {
    Spot::InputState input;
    Spot::Scene scene;
    auto& canvas = scene.CreateEntity("Canvas");
    canvas.AddComponent<Spot::Canvas>();
    Button(canvas, "Button", {0, 0});
    auto* layout = &scene.GetOrAddSystem<Spot::UiSystem>();
    auto* ui = &scene.GetOrAddSystem<Spot::UiInputSystem>();
    for (int frame = 0; frame < 3; ++frame) {
        scene.GetOrAddSystem<Spot::UiSystem>().Layout({800, 600});
        scene.GetOrAddSystem<Spot::UiInputSystem>().HandleUiEvent(Spot::KeyPressedEvent{Spot::Key::Tab, false},
                                                                  {800, 600}, &input);
        scene.Update(0);
        CHECK(&scene.GetOrAddSystem<Spot::UiSystem>() == layout);
        CHECK(&scene.GetOrAddSystem<Spot::UiInputSystem>() == ui);
    }
}

TEST_CASE("Consumed controls keep ownership through release after UI closes", "[ui][input]") {
    Spot::InputState input;
    Spot::Scene scene;
    auto& root = scene.CreateEntity("Modal");
    root.AddComponent<Spot::Canvas>().settings.modal = true;
    Button(root, "Button", {10, 10});
    auto send = [&](const Spot::Event& event) {
        const bool used = scene.GetOrAddSystem<Spot::UiInputSystem>().HandleUiEvent(event, {800, 600}, &input);
        if (!used)
            input.OnEvent(event);
        return used;
    };
    CHECK(send(Spot::KeyPressedEvent{Spot::Key::Enter, false}));
    CHECK(send(Spot::GamepadButtonPressedEvent{Spot::GamepadId{1}, Spot::GamepadButton::South}));
    root.SetActive(false);
    CHECK(send(Spot::KeyReleasedEvent{Spot::Key::Enter}));
    CHECK(send(Spot::GamepadButtonReleasedEvent{Spot::GamepadId{1}, Spot::GamepadButton::South}));
    CHECK_FALSE(input.IsKeyReleased(Spot::Key::Enter));
    CHECK_FALSE(input.IsGamepadButtonReleased(Spot::GamepadButton::South));
}
