#include "core/editor_log.h"

namespace Spot::Editor {

Logger& GetEditorLogger() {
    static Logger logger("Editor");
    return logger;
}

} // namespace Spot::Editor
