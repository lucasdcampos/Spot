#pragma once

#include <string>

namespace Spot {

// Imported source text, shared by components whose execution state remains independent.
struct ScriptSource {
    std::string source;
    // The relative source path from the asset manifest, usable in exported games too.
    std::string sourceName;
};

} // namespace Spot
