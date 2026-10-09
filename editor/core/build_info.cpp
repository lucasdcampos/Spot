// The only file that knows how the editor was built, so a change to that rebuilds nothing else.
#include "core/build_info.h"

namespace Spot::Editor {

std::filesystem::path GetBuiltPlayer() {
    return Utf8ToPath(SPOT_PLAYER);
}

} // namespace Spot::Editor
