#pragma once

#include <functional>
#include <string_view>

namespace Spot {

class Window;

enum class MessageBoxIcon {
    Information,
    Warning,
    Error,
};

// The OS's modal message boxes, for errors, warnings and diagnostics.
namespace MessageBox {

using Handler =
    std::function<bool(std::string_view title, std::string_view message, MessageBoxIcon icon, Window* window)>;

// Shows a modal message box with an OK button above the window, if any, and waits for the user to close it.
// Returns false, after logging why, when the platform cannot show it.
bool Show(std::string_view title, std::string_view message, MessageBoxIcon icon = MessageBoxIcon::Information,
          Window* window = nullptr);

// Replaces the OS dialog, as headless tests do; an empty handler restores the OS dialog.
void SetHandler(Handler handler);

} // namespace MessageBox

} // namespace Spot
