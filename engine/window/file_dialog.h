#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace Spot {

class Window;

// Files a dialog offers, such as {"Images", "png;jpg"}: extensions without dots separated by semicolons, or "*".
struct FileFilter {
    std::string name;
    std::string extensions;
};

// The OS's file dialogs. They never block: each returns at once and calls back once the user has chosen, on the
// main thread, while the application polls events.
namespace FileDialog {

// Receives the chosen paths; none when the user cancelled or the dialog could not open.
using Callback = std::function<void(std::vector<std::filesystem::path> paths)>;

// The window, which may be null, is the one the dialog stays above. The location is the folder or file it starts at.
void OpenFile(Window* window, std::span<const FileFilter> filters, Callback callback, bool allowMany = false,
              const std::filesystem::path& location = {});
void SaveFile(Window* window, std::span<const FileFilter> filters, Callback callback,
              const std::filesystem::path& location = {});
void OpenFolder(Window* window, Callback callback, const std::filesystem::path& location = {});

} // namespace FileDialog

} // namespace Spot
