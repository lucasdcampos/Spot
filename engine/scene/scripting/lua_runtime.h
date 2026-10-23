#pragma once

#include "scene/scripting/script_runtime.h"

namespace Spot::Detail {

std::unique_ptr<ScriptRuntime> CreateLuaRuntime(std::string_view text, std::string_view name);
std::optional<ScriptDiagnostic> CheckLuaSyntax(std::string_view text, std::string_view name);

} // namespace Spot::Detail
