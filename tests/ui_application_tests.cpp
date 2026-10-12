#include <spot.h>

#include <catch2/catch_test_macros.hpp>

namespace {
class UiApplication final : public Spot::Application {
public:
    UiApplication()
        : Application({.window = {.title = "UI tests", .width = 320, .height = 240},
                       .graphics = {.enabled = false},
                       .audio = {.enabled = false}}) {}
    int clicks = 0;

protected:
    void OnStart() override {
        auto scene = std::make_unique<Spot::Scene>();
        auto& root = scene->CreateEntity("Canvas");
        root.AddComponent<Spot::Canvas>();
        auto& entity = root.CreateChild("Button");
        auto& rect = entity.AddComponent<Spot::UiRect>();
        rect.layout.anchorMin = rect.layout.anchorMax = rect.layout.pivot = {};
        rect.layout.offset = {10, 10};
        rect.layout.sizeDelta = {100, 40};
        auto& button = entity.AddComponent<Spot::UiButton>();
        m_click = button.OnClick.Connect([this] { ++clicks; });
        auto& field = root.CreateChild("Field");
        field.AddComponent<Spot::UiRect>();
        field.AddComponent<Spot::UiInputField>();
        m_field = field.GetId();
        LoadScene(std::move(scene));
        Spot::Input::GetActionMap()
            ->Add("Fire", Spot::InputActionType::Button)
            .Bind(Spot::InputControl::Of(Spot::MouseButton::Left));
    }
    void OnUpdate(float) override {
        DispatchEvent(Spot::MouseMovedEvent{{20, 220}, {}});
        DispatchEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left});
        DispatchEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left});
        Spot::Input::UpdateActions();
        CHECK(clicks == 1);
        CHECK_FALSE(Spot::Input::IsActionPressed("Fire"));
        CHECK_FALSE(GetInputState().IsMouseButtonPressed(Spot::MouseButton::Left));
        CHECK(GetScene()->GetUiInput().Focus(m_field));
        DispatchEvent(Spot::TextInputEvent{"Hello"});
        CHECK(GetWindow().IsTextInputActive());
        CHECK(GetScene()->GetEntity(m_field)->GetComponent<Spot::UiInputField>()->GetText() == "Hello");
        CHECK(GetInputState().GetTypedText().empty());
        DispatchEvent(Spot::KeyPressedEvent{Spot::Key::Escape, false});
        CHECK_FALSE(GetWindow().IsTextInputActive());
        Quit();
    }

private:
    Spot::SignalConnection m_click;
    Spot::EntityId m_field = Spot::EntityId::Invalid;
};
} // namespace

TEST_CASE("Application routes UI before game actions and starts native text input", "[ui][input][application]") {
    UiApplication application;
    CHECK(application.Run() == 0);
    CHECK(application.clicks == 1);
}
