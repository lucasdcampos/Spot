#include "spot/scene/scene_system.h"

#include "spot/core/diagnostics.h"
#include "spot/core/log.h"

#include <typeinfo>

namespace Spot {

void SceneSystem::Fault(std::string_view hook, const std::exception_ptr& error) {
    m_faulted = true;
    Log::GetEngineLogger().Error("The system {} threw in {}(): {}. It is skipped until the scene is reloaded.",
                                 Detail::GetTypeName(typeid(*this)), hook, Detail::DescribeException(error));
}

} // namespace Spot
