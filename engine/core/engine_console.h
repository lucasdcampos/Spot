#pragma once

#include "core/console.h"

namespace Spot {

class Application;

// Engine bindings follow the application's current scene, so changes and reloads keep no component pointers.
class EngineConsole {
public:
    EngineConsole(Console& console, Application& application);

private:
    std::vector<ConsoleRegistration> m_registrations;
};

} // namespace Spot
