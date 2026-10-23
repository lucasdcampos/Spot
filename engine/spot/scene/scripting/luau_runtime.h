#pragma once

#include "spot/scene/scripting/script_runtime.h"

namespace Spot::Detail {

std::unique_ptr<ScriptRuntime> CreateLuauRuntime(std::string_view text, std::string_view name);
std::optional<ScriptDiagnostic> CheckLuauSyntax(std::string_view text, std::string_view name);
std::vector<ScriptExport> ReadLuauExports(std::string_view text, std::string_view name);

} // namespace Spot::Detail
