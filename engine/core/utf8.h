#pragma once

#include <string>
#include <string_view>

namespace Spot {

// Replaces bytes that are not valid UTF-8.
inline constexpr char32_t ReplacementCharacter = U'�';

// Decodes UTF-8 into code points. Invalid or truncated sequences become ReplacementCharacter, one per bad byte, so
// the rest of the text still decodes.
std::u32string DecodeUtf8(std::string_view text);

} // namespace Spot
