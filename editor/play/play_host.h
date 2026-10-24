#pragma once

#include <spot.h>

namespace Spot::Editor {

// An application that plays games inside it, as the editor does, switching to the settings of the game it plays.
class PlayHost : public Application {
public:
    using Application::Application;
    using Application::SetGameSettings;
};

} // namespace Spot::Editor
