#pragma once

#include <spot.h>

#include <filesystem>

namespace Spot::Editor {

// The player built along with the editor, which runs games outside of it.
std::filesystem::path GetBuiltPlayer();

} // namespace Spot::Editor
