#include "core/text.h"

namespace Spot::Editor {

namespace {

bool IsUpper(char c) {
    return std::isupper(static_cast<unsigned char>(c)) != 0;
}

bool IsLower(char c) {
    return std::islower(static_cast<unsigned char>(c)) != 0;
}

bool IsDigit(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

} // namespace

std::string MakeDisplayName(std::string_view name) {
    if (name.starts_with("m_")) {
        name.remove_prefix(2);
    }
    std::string result;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '_') {
            if (!result.empty() && result.back() != ' ') {
                result += ' ';
            }
            continue;
        }
        if (i > 0 && !result.empty() && result.back() != ' ' && IsUpper(c)) {
            const char previous = name[i - 1];
            const bool nextIsLower = i + 1 < name.size() && IsLower(name[i + 1]);
            // A word starts after a lower-case letter, and at the last capital of an acronym, as "HDR|Color".
            if (IsLower(previous) || ((IsUpper(previous) || IsDigit(previous)) && nextIsLower)) {
                result += ' ';
            }
        }
        const bool startsWord = result.empty() || result.back() == ' ';
        result += startsWord ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
    }
    return result;
}

} // namespace Spot::Editor
