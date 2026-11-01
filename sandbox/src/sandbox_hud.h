#pragma once

#include "gameplay/hud/hud_elements.h"

#include <spot.h>

#include <string>
#include <vector>

// What Use would do, or, while placing a prop from the spawn menu or carrying one, how to put it down.
class SandboxPromptElement final : public Gameplay::PromptElement {
public:
    SandboxPromptElement() { hideWhen = {"InVehicle"}; }

protected:
    std::string GetPrompt(const Gameplay::HudContext& context) const override;
};

struct PlayerLabel {
    std::string name;
    Spot::Vector2 position;
    float opacity = 1.0f;
    float immortalTime = 0.0f;
    bool developerAuthenticated = false;
};

// Nearby visible players, projected into the HUD's Y-up coordinates.
std::vector<PlayerLabel> GetPlayerLabels(Spot::Scene& scene, Spot::Vector2 viewSize);
void DrawPlayerLabels(Spot::Renderer2D& renderer, Spot::Scene& scene, Spot::Vector2 viewSize);

// A small shield, procedural so it stays within the public quad renderer; marks a developer-authenticated player
// above their head and beside their name in chat.
void DrawDeveloperBadge(Spot::Renderer2D& renderer, Spot::Vector2 center, float opacity);
