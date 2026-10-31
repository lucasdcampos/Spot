#pragma once

#include <spot/core/io/utf8.h>

#include <algorithm>
#include <string_view>

namespace Gameplay {

inline bool IsValidPlayerName(std::string_view name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    const auto characters = Spot::DecodeUtf8(name);
    return std::ranges::none_of(characters, [](char32_t c) {
        return c <= 0x20 || (c >= 0x7f && c <= 0x9f) || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
               c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 ||
               c == Spot::ReplacementCharacter;
    });
}

} // namespace Gameplay
