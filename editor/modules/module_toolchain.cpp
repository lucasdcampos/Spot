// The only file that knows how the editor was built, so a change to that rebuilds nothing else.
#include "modules/module_build.h"

namespace Spot::Editor {

ModuleToolchain ModuleToolchain::GetEditor() {
    return {
        .cmake = SPOT_MODULE_CMAKE,
        .generator = SPOT_MODULE_GENERATOR,
        .platform = SPOT_MODULE_PLATFORM,
        .toolset = SPOT_MODULE_TOOLSET,
        .instance = SPOT_MODULE_INSTANCE,
        .compiler = SPOT_MODULE_COMPILER,
        .makeProgram = SPOT_MODULE_MAKE_PROGRAM,
        .configuration = SPOT_MODULE_CONFIGURATION,
        .multiConfiguration = SPOT_MODULE_MULTI_CONFIGURATION != 0,
        .package = Utf8ToPath(SPOT_MODULE_PACKAGE),
    };
}

} // namespace Spot::Editor
