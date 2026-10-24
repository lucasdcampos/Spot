#pragma once

#include "spot/core/debug/profiler.h"

#include <optional>
#include <string>

namespace Spot {

// A versioned single-line record carried by the same output pipe as game logs. Names are interned when decoded.
std::string EncodeProfileFrame(const ProfileFrame& frame);
std::optional<ProfileFrame> DecodeProfileFrame(std::string_view line);
inline constexpr std::string_view ProfileStreamPrefix = "@spot-profile ";

} // namespace Spot
