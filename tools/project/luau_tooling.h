#pragma once

#include "project/project.h"

#include <string>

namespace Spot {

// The module `require("@spot")` resolves to for Luau's language server (luau-lsp): it gives nothing when run, but
// its type, built from the engine's script API (ScriptApi), describes every engine object scripts reach from it.
std::string MakeLuauModule();
// Luau's definition file syntax for the globals the engine gives scripts, the value constructors (Vector3()...).
// Language servers load it as their editor's settings say, such as luau-lsp's definition files.
std::string MakeLuauDefinitions();

// Writes both into the project's generated files: .spot/luau/spot.luau and .spot/luau/spot.d.luau. Returns false,
// after logging why, when they cannot be written.
bool WriteLuauTypes(const Project& project);

// Also points the "spot" alias of the project's .luaurc at the module, so any editor with Luau's language server
// checks the scripts against the engine's API. A project's own .luaurc only gets the alias; a new one also turns off
// the lint about unused global functions, which are what the engine and other scripts call.
bool WriteLuauTooling(const Project& project);

} // namespace Spot
