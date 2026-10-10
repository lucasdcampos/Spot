#include "shadow_toggle.h"

SPOT_COMPONENT(ShadowToggle);

void ShadowToggle::OnUpdate(float /*deltaTime*/) {
    if (!Spot::Input::IsActionPressed("Shadows")) {
        return;
    }
    // Lights follow the first one, so the switch never leaves some on and some off.
    bool shadows = false;
    bool first = true;
    GetEntity().GetScene().ForEachComponent<Spot::Light>([&](Spot::Light& light) {
        if (first) {
            shadows = !light.IsCastingShadows();
            first = false;
        }
        light.SetCastingShadows(shadows);
    });
}
