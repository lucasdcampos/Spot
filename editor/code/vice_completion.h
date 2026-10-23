#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Spot::Editor {

// A member the code editor suggests after a dot.
struct ViceCompletion {
    std::string name;
    bool method = false;
    // What the property holds or the method returns, as ScriptApi names types; empty when unknown or nothing.
    std::string type;
};

// The members of what comes before the dot at `position`, a byte offset into the source where the member being
// typed starts, whose names start with `prefix`, ignoring case. Like highlighting, it reads the source without
// parsing it, so unfinished code works: the receiver is a chain of names and calls ("entity.transform",
// "GetComponent(\"Camera\")") that starts at what a component script knows the type of: its implicit members
// (entity, transform, scene and the inherited methods), self, the spot module, a hook's parameter, or a value
// constructor. Local variables are not followed. Empty when the type is unknown.
std::vector<ViceCompletion> GetViceMemberCompletions(std::string_view source, std::size_t position,
                                                     std::string_view prefix);

} // namespace Spot::Editor
