#include "version.h"

namespace Spot {

Version GetVersion() {
    return Version(SPOT_VERSION_MAJOR, SPOT_VERSION_MINOR, SPOT_VERSION_PATCH);
}

std::string_view GetVersionString() {
    return SPOT_VERSION_STRING;
}

} // namespace Spot
