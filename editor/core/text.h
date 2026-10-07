#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Spot::Editor {

// Whether the text has the part in it, ignoring the case of ASCII letters, as search fields match. Every text has
// the empty part.
inline bool ContainsIgnoringCase(std::string_view text, std::string_view part) {
    const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return part.empty() || !std::ranges::search(text, part, {}, lower, lower).empty();
}

// A name from code as words: "castsShadows" as "Casts Shadows", "DirectionalLight" as "Directional Light" and
// "HDRColor" as "HDR Color".
std::string MakeDisplayName(std::string_view name);

} // namespace Spot::Editor
