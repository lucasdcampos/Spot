#include "tools_log.h"

namespace Spot {

Logger& GetToolsLogger() {
    static Logger logger("Tools");
    return logger;
}

} // namespace Spot
