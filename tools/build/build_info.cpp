// The only file that knows how the tools were built, so a change to that rebuilds nothing else.
#include "build/code_build.h"

namespace Spot {

Toolchain Toolchain::GetBuilt() {
    return {
        .cmake = SPOT_TOOLCHAIN_CMAKE,
        .generator = SPOT_TOOLCHAIN_GENERATOR,
        .platform = SPOT_TOOLCHAIN_PLATFORM,
        .toolset = SPOT_TOOLCHAIN_TOOLSET,
        .instance = SPOT_TOOLCHAIN_INSTANCE,
        .compiler = SPOT_TOOLCHAIN_COMPILER,
        .makeProgram = SPOT_TOOLCHAIN_MAKE_PROGRAM,
        .configuration = SPOT_TOOLCHAIN_CONFIGURATION,
        .multiConfiguration = SPOT_TOOLCHAIN_MULTI_CONFIGURATION != 0,
        .package = Utf8ToPath(SPOT_TOOLCHAIN_PACKAGE),
        .engineLibrary = Utf8ToPath(SPOT_TOOLCHAIN_ENGINE_LIBRARY),
        .player = Utf8ToPath(SPOT_TOOLCHAIN_PLAYER),
    };
}

} // namespace Spot
