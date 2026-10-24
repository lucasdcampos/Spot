#include "spot/scene/ui/ui_components.h"

#include "spot/scene/component_registry.h"
#include "spot/scene/entity.h"
#include "spot/scene/ui/canvas.h"
#include "spot/scene/ui/ui_layout_group.h"
#include "spot/scene/ui/ui_widgets.h"

namespace Spot {

void RegisterUiComponents() {
    RegisterComponent<UiLayoutGroup>("UiLayoutGroup");
    RegisterComponent<UiSizeFitter>("UiSizeFitter");
    RegisterComponent<UiImage>("UiImage");
    RegisterComponent<UiText>("UiText");
    RegisterComponent<UiButton>("UiButton");
    RegisterComponent<UiToggle>("UiToggle");
    RegisterComponent<UiSlider>("UiSlider");
    RegisterComponent<UiInputField>("UiInputField");
    RegisterComponent<UiScrollView>("UiScrollView");
    RegisterComponent<Canvas>("Canvas");
    RegisterComponent<UiRect>("UiRect");
}

} // namespace Spot
