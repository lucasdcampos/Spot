#include "platform/platform.h"

#include <spot.h>

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <vector>

namespace {

std::vector<Spot::Event> PollEvents() {
    std::vector<Spot::Event> events;
    Spot::Platform::PollEvents([&](const Spot::Event& event) { events.push_back(event); });
    return events;
}

// Initializes the platform and drains whatever SDL queued during startup, so tests only see their own events.
class ScopedPlatform {
public:
    ScopedPlatform() {
        REQUIRE(Spot::Platform::Initialize());
        PollEvents();
    }

    ~ScopedPlatform() { Spot::Platform::Shutdown(); }

    ScopedPlatform(const ScopedPlatform&) = delete;
    ScopedPlatform& operator=(const ScopedPlatform&) = delete;
};

void Push(SDL_Event event) {
    REQUIRE(SDL_PushEvent(&event));
}

SDL_Event MakeKeyEvent(SDL_EventType type, SDL_Scancode scancode, bool repeat) {
    SDL_Event event{};
    event.type = type;
    event.key.scancode = scancode;
    event.key.repeat = repeat;
    return event;
}

SDL_Event MakeWindowEvent(SDL_EventType type, Sint32 data1 = 0, Sint32 data2 = 0) {
    SDL_Event event{};
    event.type = type;
    event.window.data1 = data1;
    event.window.data2 = data2;
    return event;
}

} // namespace

TEST_CASE("SDL key events become events with physical keys", "[platform][sdl3]") {
    const ScopedPlatform platform;
    Push(MakeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W, false));
    Push(MakeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_KP_ENTER, true));
    Push(MakeKeyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_NONUSBACKSLASH, false));
    Push(MakeKeyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_MEDIA_PLAY, false));

    const auto events = PollEvents();

    REQUIRE(events.size() == 4);
    const auto* w = events[0].GetIf<Spot::KeyPressedEvent>();
    REQUIRE(w != nullptr);
    CHECK(w->key == Spot::Key::W);
    CHECK_FALSE(w->repeat);
    const auto* enter = events[1].GetIf<Spot::KeyPressedEvent>();
    REQUIRE(enter != nullptr);
    CHECK(enter->key == Spot::Key::NumpadEnter);
    CHECK(enter->repeat);
    const auto* intl = events[2].GetIf<Spot::KeyReleasedEvent>();
    REQUIRE(intl != nullptr);
    CHECK(intl->key == Spot::Key::IntlBackslash);
    const auto* unknown = events[3].GetIf<Spot::KeyReleasedEvent>();
    REQUIRE(unknown != nullptr);
    CHECK(unknown->key == Spot::Key::Unknown);
}

TEST_CASE("SDL mouse events become mouse events", "[platform][sdl3]") {
    const ScopedPlatform platform;

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 10.0f;
    motion.motion.y = 20.0f;
    motion.motion.xrel = 1.0f;
    motion.motion.yrel = -2.0f;
    Push(motion);

    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.button = SDL_BUTTON_RIGHT;
    Push(button);
    button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    button.button.button = SDL_BUTTON_X2;
    Push(button);
    button.button.button = 9;
    Push(button);

    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 1.0f;
    wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    Push(wheel);

    const auto events = PollEvents();

    REQUIRE(events.size() == 4);
    const auto* moved = events[0].GetIf<Spot::MouseMovedEvent>();
    REQUIRE(moved != nullptr);
    CHECK(moved->position == Spot::Vector2{10.0f, 20.0f});
    CHECK(moved->delta == Spot::Vector2{1.0f, -2.0f});
    const auto* pressed = events[1].GetIf<Spot::MouseButtonPressedEvent>();
    REQUIRE(pressed != nullptr);
    CHECK(pressed->button == Spot::MouseButton::Right);
    const auto* released = events[2].GetIf<Spot::MouseButtonReleasedEvent>();
    REQUIRE(released != nullptr);
    CHECK(released->button == Spot::MouseButton::X2);
    const auto* scrolled = events[3].GetIf<Spot::MouseScrolledEvent>();
    REQUIRE(scrolled != nullptr);
    CHECK(scrolled->delta.y == -1.0f);
}

TEST_CASE("SDL window and quit events become window events", "[platform][sdl3]") {
    const ScopedPlatform platform;
    Push(MakeWindowEvent(SDL_EVENT_WINDOW_RESIZED, 640, 480));
    Push(MakeWindowEvent(SDL_EVENT_WINDOW_FOCUS_LOST));
    Push(MakeWindowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED));
    Push(MakeWindowEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED));
    Push(MakeWindowEvent(SDL_EVENT_QUIT));

    const auto events = PollEvents();

    REQUIRE(events.size() == 5);
    const auto* resize = events[0].GetIf<Spot::WindowResizeEvent>();
    REQUIRE(resize != nullptr);
    CHECK(resize->width == 640);
    CHECK(resize->height == 480);
    const auto* focusLost = events[1].GetIf<Spot::WindowFocusEvent>();
    REQUIRE(focusLost != nullptr);
    CHECK_FALSE(focusLost->focused);
    const auto* focusGained = events[2].GetIf<Spot::WindowFocusEvent>();
    REQUIRE(focusGained != nullptr);
    CHECK(focusGained->focused);
    CHECK(events[3].Is<Spot::WindowCloseEvent>());
    CHECK(events[4].Is<Spot::WindowCloseEvent>());
}

TEST_CASE("Application feeds native events to OnEvent and Input, and stops when the window closes",
          "[platform][sdl3]") {
    struct Observed {
        int updates = 0;
        bool keyPressedInFirstUpdate = false;
        bool closeSeen = false;
    };

    class ClosingApplication final : public Spot::Application {
    public:
        explicit ClosingApplication(Observed& observed) : m_observed(observed) {}

    protected:
        void OnStart() override { Push(MakeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W, false)); }

        void OnEvent(const Spot::Event& event) override {
            m_observed.closeSeen = m_observed.closeSeen || event.Is<Spot::WindowCloseEvent>();
        }

        void OnUpdate(float) override {
            if (++m_observed.updates == 1) {
                m_observed.keyPressedInFirstUpdate = Spot::Input::IsKeyPressed(Spot::Key::W);
                Push(MakeWindowEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED));
            } else {
                Quit();
            }
        }

    private:
        Observed& m_observed;
    };

    Observed observed;
    ClosingApplication application(observed);

    CHECK(application.Run() == EXIT_SUCCESS);

    CHECK(observed.keyPressedInFirstUpdate);
    CHECK(observed.closeSeen);
    CHECK(observed.updates == 1);
    CHECK_FALSE(Spot::Input::IsKeyDown(Spot::Key::W));
}
