#pragma once

#include <spot.h>

#include <cstdint>

// Change whenever the sandbox's network messages or replicated content layout change.
inline constexpr std::uint32_t SandboxProtocol = 6;

// Snapshots twice as often as the engine's default, shown half as far in the past, for rooms on a local network where
// what the server decides, as props and shots, should follow closely.
inline Spot::NetworkModuleConfig SandboxNetworkConfig() {
    return {.protocol = SandboxProtocol, .system = {.sendRate = 60.0f, .interpolationDelay = 0.05f}};
}
