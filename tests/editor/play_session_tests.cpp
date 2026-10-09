#include "play/play_session.h"

#include <spot.h>
#include <spot_tools.h>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

using Spot::Entity;
using Spot::EntityId;

namespace {

// Moves its entity one unit up each update, and remembers whether it found its application.
class Climber final : public Spot::Component {
public:
    int updates = 0;
    bool foundApplication = false;

protected:
    void OnUpdate(float /*deltaTime*/) override {
        ++updates;
        foundApplication = GetScene().IsLoaded();
        Spot::Transform& transform = GetEntity().GetTransform();
        transform.SetPosition(transform.GetPosition() + Spot::Vector3{0.0f, 1.0f, 0.0f});
    }

    void Serialize(Spot::Archive&) override {}
};

// Runs a function on each frame, after the scenes updated in the frame before, until it returns false.
class FrameApplication final : public Spot::Application {
public:
    explicit FrameApplication(std::function<bool(FrameApplication&, int)> frame)
        : Application({.window = {.title = "Play Tests", .width = 320, .height = 240},
                       .graphics = {.enabled = false},
                       .audio = {.enabled = false}}),
          m_frame(std::move(frame)) {}

    using Application::DispatchEvent;

protected:
    void OnUpdate(float /*deltaTime*/) override {
        if (!m_frame(*this, ++m_count) || m_count > 50) {
            Quit();
        }
    }

private:
    std::function<bool(FrameApplication&, int)> m_frame;
    int m_count = 0;
};

} // namespace

TEST_CASE("Playing runs a copy of the scene as a game, and stopping leaves the scene as it was", "[editor][play]") {
    Spot::RegisterComponent<Climber>("Climber");
    Spot::Editor::SceneDocument source("Level");
    Entity& climber = source.GetScene().CreateEntity("Climber");
    climber.AddComponent<Climber>();
    source.GetSelection().Select(climber.GetId());

    std::unique_ptr<Spot::Editor::PlaySession> play;
    Entity* copy = nullptr;
    std::vector<int> updates;
    bool selectedCopy = false;
    bool sawApplication = false;
    bool keyWhileUnfocused = true;
    bool keyWhileFocused = false;
    const Spot::InputState* inputWhilePlaying = nullptr;

    FrameApplication application([&](FrameApplication& app, int frame) {
        switch (frame) {
        case 1:
            play = std::make_unique<Spot::Editor::PlaySession>(app, source, app.GetAssets());
            inputWhilePlaying = Spot::Input::GetState();
            break;
        case 3: {
            play->Update();
            Spot::Editor::SceneDocument* document = play->GetDocument();
            REQUIRE(document);
            copy = document->GetScene().FindEntity("Climber");
            REQUIRE(copy);
            selectedCopy = document->GetSelection().GetPrimary() == copy->GetId();
            sawApplication = copy->GetComponent<Climber>()->foundApplication;
            updates.push_back(copy->GetComponent<Climber>()->updates);
            play->SetPaused(true);
            // Keys reach the game only while its view has focus.
            play->Feed(Spot::KeyPressedEvent{.key = Spot::Key::Space, .repeat = false});
            keyWhileUnfocused = Spot::Input::IsKeyDown(Spot::Key::Space);
            play->SetView(true, {10.0f, 20.0f});
            play->Feed(Spot::KeyPressedEvent{.key = Spot::Key::Space, .repeat = false});
            keyWhileFocused = Spot::Input::IsKeyDown(Spot::Key::Space);
            break;
        }
        case 5:
            updates.push_back(copy->GetComponent<Climber>()->updates);
            play->Step();
            break;
        case 6:
            updates.push_back(copy->GetComponent<Climber>()->updates);
            // Selected in the copy, then selected in the scene once playing stops.
            play->GetDocument()->GetSelection().Clear();
            play.reset();
            break;
        case 8:
            CHECK(app.GetScene() == nullptr);
            CHECK(Spot::Input::GetState() != inputWhilePlaying);
            return false;
        default:
            break;
        }
        return true;
    });
    REQUIRE(application.Run() == EXIT_SUCCESS);

    CHECK(selectedCopy);
    CHECK(sawApplication);
    CHECK_FALSE(keyWhileUnfocused);
    CHECK(keyWhileFocused);
    // Frames 1 and 2 load and update the copy; it is paused at frame 3, and runs one step at frame 5.
    CHECK(updates == std::vector<int>{1, 1, 2});
    // The scene played is untouched.
    CHECK(climber.GetComponent<Climber>()->updates == 0);
    CHECK(climber.GetTransform().GetPosition().y == 0.0f);
    CHECK(source.GetSelection().IsEmpty());
    Spot::UnregisterComponent("Climber");
}

namespace {

// Locks the cursor, as first-person games do, and remembers whether it ever saw Escape.
class CursorLocker final : public Spot::Component {
public:
    bool sawEscape = false;

protected:
    void OnUpdate(float /*deltaTime*/) override {
        Spot::Input::SetCursorLocked(true);
        sawEscape = sawEscape || Spot::Input::IsKeyDown(Spot::Key::Escape);
    }

    void Serialize(Spot::Archive&) override {}
};

} // namespace

TEST_CASE("The game captures the mouse while its view has focus, and Escape stays the editor's", "[editor][play]") {
    Spot::RegisterComponent<CursorLocker>("CursorLocker");
    Spot::Editor::SceneDocument source("Level");
    source.GetScene().CreateEntity("Player").AddComponent<CursorLocker>();

    std::unique_ptr<Spot::Editor::PlaySession> play;
    std::vector<bool> captured;
    bool sawEscape = true;

    FrameApplication application([&](FrameApplication& app, int frame) {
        switch (frame) {
        case 1:
            play = std::make_unique<Spot::Editor::PlaySession>(app, source, app.GetAssets());
            break;
        case 3:
            // The game locked the cursor, which waits for its view to have focus.
            play->Update();
            play->SetView(false, {});
            captured.push_back(app.GetWindow().IsMouseCaptured());
            play->SetView(true, {});
            captured.push_back(app.GetWindow().IsMouseCaptured());
            play->Feed(Spot::KeyPressedEvent{.key = Spot::Key::Escape, .repeat = false});
            break;
        case 4: {
            play->Update();
            const auto* locker = play->GetDocument()->GetScene().FindComponent<CursorLocker>();
            REQUIRE(locker);
            sawEscape = locker->sawEscape;
            play->SetView(false, {});
            captured.push_back(app.GetWindow().IsMouseCaptured());
            play->SetView(true, {});
            play.reset();
            captured.push_back(app.GetWindow().IsMouseCaptured());
            return false;
        }
        default:
            break;
        }
        return true;
    });
    REQUIRE(application.Run() == EXIT_SUCCESS);

    CHECK(captured == std::vector<bool>{false, true, false, false});
    CHECK_FALSE(sawEscape);
    Spot::UnregisterComponent("CursorLocker");
}

TEST_CASE("Lines of the game's log keep their level, and say which logger wrote them", "[editor][play]") {
    using Spot::GameRunner;

    const GameRunner::Line own = GameRunner::ParseLine("[14:03:12.345] [WARN ] [App] Low on health");
    CHECK(own.level == Spot::LogLevel::Warning);
    CHECK(own.message == "Low on health");

    const GameRunner::Line engine = GameRunner::ParseLine("[14:03:12.345] [ERROR] [Spot] Could not open 'a.png'");
    CHECK(engine.level == Spot::LogLevel::Error);
    CHECK(engine.message == "Spot: Could not open 'a.png'");

    const GameRunner::Line other = GameRunner::ParseLine("  the second line of a message");
    CHECK(other.level == Spot::LogLevel::Info);
    CHECK(other.message == "  the second line of a message");
    CHECK(GameRunner::ParseLine("[14:03] [INFO ]").message == "[14:03] [INFO ]");
}
