#pragma once

#include <spot_tools.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Spot::Editor {

// The program and arguments of the user's external editor command, split as a shell splits words, double quotes
// keeping spaces. {project} becomes the project's folder, {file} the file to open (the project's folder when there
// is none) and {line} its line (1 when not given). A command with neither {project} nor {file} gets what to open
// added at the end, so "code" or "zed" alone open it too.
std::vector<std::string> MakeExternalEditorCommand(std::string_view command, const std::filesystem::path& project,
                                                   const std::filesystem::path& file = {}, std::size_t line = 0);

// Opens the project, or one of its files, in the user's code editor: with the command when there is one, or else
// with what the OS opens it with. The project's Luau tooling is written first (see WriteLuauTooling()), so the editor's
// language server knows the engine's API. Returns false, after logging why, when it cannot.
bool OpenInExternalEditor(std::string_view command, const Project& project, const std::filesystem::path& file = {},
                          std::size_t line = 0);

} // namespace Spot::Editor
