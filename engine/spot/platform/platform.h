#pragma once

#include "spot/platform/event.h"

#include <chrono>
#include <functional>

// Contract between the engine and the windowing backend chosen with SPOT_PLATFORM. No backend type appears in it,
// so switching backends leaves the public API untouched; loops of their own poll events with it.
namespace Spot::Platform {

// Calls nest: every successful Initialize() needs a matching Shutdown().
bool Initialize();
void Shutdown();

using EventCallback = std::function<void(const Event&)>;

// Delivers the pending native events, translated into engine events, in the order they happened.
void PollEvents(const EventCallback& callback);

// Sleeps more precisely than the OS default timer allows, which frame limiting needs.
void SleepFor(std::chrono::nanoseconds duration);

// Shakes the gamepad's low and high frequency motors, each from 0 to 1, for the time; a new call replaces the last.
// Returns false when the gamepad is not connected or has no motors.
bool RumbleGamepad(GamepadId gamepad, float low, float high, std::chrono::milliseconds duration);

} // namespace Spot::Platform
