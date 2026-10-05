#pragma once

// Contract between the engine and the windowing backend chosen with SPOT_PLATFORM. Only engine sources use it
// and no backend type appears in it, so switching backends leaves the public API untouched.
namespace Spot::Platform {

// Calls nest: every successful Initialize() needs a matching Shutdown().
bool Initialize();
void Shutdown();

} // namespace Spot::Platform
