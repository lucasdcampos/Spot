#pragma once

// Where the mouse was when the window captured it, kept in the SDL window's properties. SDL still moves its own
// mouse position while capturing, and puts the cursor there on release, so the backend reports and restores this one
// instead: a captured mouse stays put.
namespace Spot::Platform {

inline constexpr const char* CapturedMouseXProperty = "spot.captured_mouse.x";
inline constexpr const char* CapturedMouseYProperty = "spot.captured_mouse.y";

} // namespace Spot::Platform
