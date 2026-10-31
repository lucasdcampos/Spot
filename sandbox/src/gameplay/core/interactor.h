#pragma once

#include "gameplay/core/input_context.h"
#include "gameplay/core/player_input.h"

#include <spot.h>
#include <spot/scene/physics/physics_query.h>

#include <optional>
#include <string>
#include <vector>

namespace Gameplay {

class InteractionHandler;
class Interactable;
class Pawn;

// On a pawn: looks along its view point within reach for an Interactable on the entity hit or above it, failing that
// for an InteractionHandler on its own entity that would use what was hit, and uses the one it finds on its action.
// It publishes what it would use, and the prompt for it, for the HUD. Nothing is in focus while the action is taken
// by a context above its own, or while the pawn is in a state that blocks it. On a machine that does not decide the
// pawn, its action sends a reliable request; the server finds and validates the target itself.
class Interactor : public Spot::Component, public PlayerInputListener {
public:
    float reach = 3.0f;
    std::string action = "Use";
    std::vector<std::string> blockedBy = {"Dead"};

    // The entity hit that the action would use; Invalid when there is none.
    Spot::EntityId GetFocus() const { return m_focus; }
    bool HasFocus() const { return m_focus != Spot::EntityId::Invalid; }
    // What doing it does, such as "Drive"; empty without a focus.
    const std::string& GetPrompt() const { return m_prompt; }
    // Uses what is in focus now; false when nothing is. On the owning client, queues a request instead; the server
    // chooses and validates the target when it arrives.
    bool Interact();
    Pawn* GetPawn() const;

protected:
    // The action is read as it updates, after the view turned, so the input itself does nothing.
    void OnPlayerInput(PlayerController& /*player*/) override {}
    void OnPlayerRequest(std::string_view request) override;
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    struct Target {
        Spot::RaycastHit hit;
        Interactable* interactable = nullptr;
        InteractionHandler* handler = nullptr;
        std::string prompt;
    };
    std::optional<Target> FindTarget() const;
    void UpdateFocus();

    InputContext m_input{{.name = "Interact", .priority = 0, .actions = {"Use"}}};
    Spot::EntityId m_focus = Spot::EntityId::Invalid;
    std::string m_prompt;
};

} // namespace Gameplay
