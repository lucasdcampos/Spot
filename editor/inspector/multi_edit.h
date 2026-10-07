#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

// Editing several entities at once compares and copies their components' saved values, which JSON pointers such as
// "/data/color/0" point to.
namespace Spot::Editor {

// A field name as a token of a JSON pointer, where '~' and '/' are escaped.
std::string EscapePointerToken(std::string_view token);

// Pointers to the values that differ between the primary text and any of the others, all JSON of the same kind, such
// as saved components. Arrays of the same size are compared element by element, so a vector tells which of its axes
// differ; arrays of different sizes, and values of different kinds, differ as a whole.
std::vector<std::string> FindMixedValues(std::string_view primary, std::span<const std::string> others);

// The target text, with its values at the pointers replaced by the source's, which is how an edit to one entity's
// component reaches the others'. Pointers the source does not have, or whose parent the target does not have, are
// skipped. Texts that are not JSON leave the target as it is.
std::string CopyValues(std::string_view source, std::string_view target, std::span<const std::string> pointers);

} // namespace Spot::Editor
