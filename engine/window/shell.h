#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Hands files, folders and links to the OS, which opens them with the user's chosen applications.
namespace Spot::Shell {

// Opens a folder in the file manager, or a file with its default application. Returns false, after logging why,
// when the OS cannot.
bool Open(const std::filesystem::path& path);
bool OpenUrl(std::string_view url);

// The "file://" URL of a path, with the characters URLs cannot hold percent-encoded.
std::string ToFileUrl(const std::filesystem::path& path);

} // namespace Spot::Shell
