#include "spot/core/utf8.h"

#include <cstddef>
#include <cstdint>

namespace Spot {

namespace {

bool IsContinuation(unsigned char byte) {
    return (byte & 0xC0) == 0x80;
}

} // namespace

std::u32string DecodeUtf8(std::string_view text) {
    std::u32string result;
    result.reserve(text.size());

    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);

        std::size_t length = 0;
        char32_t codePoint = 0;
        char32_t minimum = 0;
        if (lead < 0x80) {
            length = 1;
            codePoint = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            length = 2;
            codePoint = lead & 0x1F;
            minimum = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            codePoint = lead & 0x0F;
            minimum = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            codePoint = lead & 0x07;
            minimum = 0x10000;
        }

        bool valid = length > 0 && i + length <= text.size();
        for (std::size_t j = 1; valid && j < length; ++j) {
            const auto byte = static_cast<unsigned char>(text[i + j]);
            valid = IsContinuation(byte);
            codePoint = (codePoint << 6) | (byte & 0x3F);
        }
        // Overlong encodings, surrogates and values past U+10FFFF are not valid UTF-8 either.
        valid = valid && codePoint >= minimum && codePoint <= 0x10FFFF && (codePoint < 0xD800 || codePoint > 0xDFFF);

        if (valid) {
            result.push_back(codePoint);
            i += length;
        } else {
            result.push_back(ReplacementCharacter);
            ++i;
        }
    }
    return result;
}

} // namespace Spot
