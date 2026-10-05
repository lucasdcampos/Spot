#pragma once

#include <string>
#include <string_view>

// The OS clipboard, shared with other applications. Text is UTF-8.
namespace Spot::Clipboard {

bool HasText();
// Empty when the clipboard holds no text.
std::string GetText();
// Returns false, after logging why, when the platform rejects it.
bool SetText(std::string_view text);

} // namespace Spot::Clipboard
