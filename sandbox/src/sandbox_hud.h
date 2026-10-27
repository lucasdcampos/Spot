#pragma once

#include "gameplay/hud/hud_elements.h"

#include <spot.h>

#include <string>

// What Use would do, or, while placing a prop from the spawn menu or carrying one, how to put it down.
class SandboxPromptElement final : public Gameplay::PromptElement {
public:
    SandboxPromptElement() { hideWhen = {"InVehicle"}; }

protected:
    std::string GetPrompt(const Gameplay::HudContext& context) const override;
};
