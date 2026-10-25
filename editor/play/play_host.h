#pragma once

#include <spot.h>

namespace Spot::Editor {

// An application that plays games inside it, as the editor does, switching to the settings of the game it plays.
class PlayHost : public Application {
public:
    using Application::Application;
    using Application::SetGameSettings;

protected:
    // The game gets the keyboard, the mouse and gamepads from PlaySession::Feed(), relative to its view and only while
    // that view has focus, so none reaches its scene, or its canvases, straight from the window.
    bool DeliversToScenes(const Event& event) override {
        return !(event.Is<KeyPressedEvent>() || event.Is<KeyReleasedEvent>() || event.Is<TextInputEvent>() ||
                 event.Is<MouseMovedEvent>() || event.Is<MouseButtonPressedEvent>() ||
                 event.Is<MouseButtonReleasedEvent>() || event.Is<MouseScrolledEvent>() ||
                 event.Is<GamepadButtonPressedEvent>() || event.Is<GamepadButtonReleasedEvent>() ||
                 event.Is<GamepadAxisEvent>());
    }
};

} // namespace Spot::Editor
