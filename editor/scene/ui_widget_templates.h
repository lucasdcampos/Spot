#pragma once

#include "scene/entity_actions.h"

namespace Spot::Editor {

bool IsUiTemplate(EntityTemplate kind);
bool CreateUiWidget(SceneDocument& document, EntityTemplate kind, EntityId parent);

} // namespace Spot::Editor
