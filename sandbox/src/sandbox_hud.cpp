#include "sandbox_hud.h"

#include "prop_spawner.h"

#include "gameplay/character/carrier.h"
#include "gameplay/core/pawn.h"
#include "gameplay/hud/input_prompts.h"

SPOT_COMPONENT(SandboxPromptElement);

std::string SandboxPromptElement::GetPrompt(const Gameplay::HudContext& context) const {
    using Gameplay::InputHint;
    const Spot::Entity* pawn = context.pawn ? &context.pawn->GetEntity() : nullptr;
    const auto* spawner = pawn ? pawn->GetComponent<PropSpawner>() : nullptr;
    if (spawner && spawner->IsPlacing()) {
        const auto spawnables = spawner->GetSpawnables();
        const std::size_t index = *spawner->GetPlacingIndex();
        const InputHint hints[] = {
            {"Fire", "Place " + (index < spawnables.size() ? spawnables[index].name : std::string())},
            {"Aim", "Cancel"}};
        return Gameplay::InputPrompts::Format(hints);
    }
    if (const auto* carrier = pawn ? pawn->GetComponent<Gameplay::Carrier>() : nullptr;
        carrier && carrier->IsHolding()) {
        return Gameplay::InputPrompts::Format(carrier->GetHints());
    }
    return PromptElement::GetPrompt(context);
}
