#pragma once

#include "events/event.h"

#include <functional>

// Contract between the engine and the windowing backend chosen with SPOT_PLATFORM. Only engine sources use it
// and no backend type appears in it, so switching backends leaves the public API untouched.
namespace Spot::Platform {

// Calls nest: every successful Initialize() needs a matching Shutdown().
bool Initialize();
void Shutdown();

using EventCallback = std::function<void(const Event&)>;

// Delivers the pending native events, translated into engine events, in the order they happened.
void PollEvents(const EventCallback& callback);

} // namespace Spot::Platform
