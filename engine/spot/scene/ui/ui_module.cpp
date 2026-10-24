#include "spot/scene/ui/ui_module.h"

#include "spot/runtime/application.h"
#include "spot/scene/ui/ui_components.h"
#include "spot/scene/ui/ui_input_system.h"

#include <algorithm>

namespace Spot {

void UiModule::OnStart() {
    RegisterUiComponents();
}

void UiModule::OnStop() {
    if (m_textInput) {
        GetApplication().GetWindow().StopTextInput();
        m_textInput = false;
    }
}

bool UiModule::OnEvent(const Event& event) {
    Application& application = GetApplication();
    Scene* scene = application.GetScene();
    if (!scene || !application.DeliversToScenes(event)) {
        return false;
    }
    Window& window = application.GetWindow();
    // Canvases lay out in pixels, while the window reports the mouse in its own units.
    Event uiEvent = event;
    if (const auto* moved = event.GetIf<MouseMovedEvent>()) {
        auto pixel = *moved;
        const float xScale =
            static_cast<float>(window.GetPixelWidth()) / static_cast<float>(std::max(1, window.GetWidth()));
        const float yScale =
            static_cast<float>(window.GetPixelHeight()) / static_cast<float>(std::max(1, window.GetHeight()));
        pixel.position = {pixel.position.x * xScale, pixel.position.y * yScale};
        uiEvent = Event(pixel);
    }
    const Vector2 size{static_cast<float>(window.GetPixelWidth()), static_cast<float>(window.GetPixelHeight())};
    InputState& input = application.GetInputState();
    const bool consumed = scene->GetOrAddSystem<UiInputSystem>().HandleUiEvent(uiEvent, size, &input);
    if (consumed) {
        input.ConsumeEvent(event);
    }
    UpdateTextInput();
    return consumed;
}

void UiModule::OnUpdate(float /*deltaTime*/) {
    UpdateTextInput();
}

void UiModule::UpdateTextInput() {
    Application& application = GetApplication();
    Window& window = application.GetWindow();
    const InputState& input = application.GetInputState();
    const bool wanted = UiInputSystem::IsTextInputCaptured(input);
    if (wanted && !window.IsTextInputActive()) {
        window.StartTextInput();
    } else if (m_textInput && !wanted && !input.IsCaptured()) {
        // Text input stays on while an overlay, such as the console, captures the input in its place.
        window.StopTextInput();
    }
    m_textInput = wanted;
}

} // namespace Spot
