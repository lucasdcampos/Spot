#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Spot {

// The folder that holds the running executable. Assets shipped next to it are found from here, whatever the current
// working directory is.
std::filesystem::path GetExecutableDirectory();

// Converts a path to UTF-8 for text such as log messages; path::string() throws on Windows for characters outside
// the system code page.
std::string PathToUtf8(const std::filesystem::path& path);
// The reverse, for paths that come as UTF-8 text, such as those inside asset files.
std::filesystem::path Utf8ToPath(std::string_view utf8);

} // namespace Spot
