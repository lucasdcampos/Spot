#pragma once

#include "gameplay/core/input_context.h"
#include "gameplay/core/interactable.h"
#include "gameplay/core/user_settings.h"
#include "gameplay/hud/input_prompts.h"
#include "gameplay/persistence/world_save.h"

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

// Picks up the body its pawn looks at, when it weighs no more than maxMass, and carries it in front of the eyes; the
// use action again drops it, the throw action throws it, and holding the turn action turns it with Look. It is the
// pawn's Interactor that picks bodies up, offering it what it hits; while one is carried, the carrier takes its
// actions from everything else and the pawn's hands are busy ("HandsBusy"). The dead drop it.
class Carrier : public Spot::Component, public InteractionHandler, public SaveListener, public UserSettingsListener {
public:
    float reach = 2.5f;
    // In kilograms.
    float maxMass = 35.0f;
    // How far in front of the eyes the body is carried, in meters.
    float holdDistance = 1.6f;
    float throwSpeed = 12.0f;
    std::string useAction = "Use";
    std::string throwAction = "Fire";
    std::string turnAction = "Aim";
    std::string lookAction = "Look";
    // Pawn states that immediately release what it carries.
    std::vector<std::string> releaseWhen = {"Dead"};

    bool IsHolding() const { return m_held != Spot::EntityId::Invalid; }
    Spot::EntityId GetHeld() const { return m_held; }
    // Whether the body hit, that far away, is one the carrier can pick up.
    bool IsCarryable(const Spot::Entity& body, float distance) const;
    void PickUp(Spot::Entity& body);
    void Drop();
    void Throw();
    // What the controls do while carrying.
    std::vector<InputHint> GetHints() const;

protected:
    void ApplySettings(const UserSettings& settings) override { m_lookSensitivity = settings.sensitivity; }
    void OnStart() override;
    void OnBeforeLoad() override { Drop(); }
    std::optional<std::string> GetInteraction(const Interactor& interactor, const Spot::RaycastHit& hit) const override;
    void Interact(Interactor& interactor, const Spot::RaycastHit& hit) override;

    void OnDisable() override { Drop(); }
    void OnUpdate(float deltaTime) override;
    void OnFixedUpdate(float fixedDeltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    // The held body's rigidbody, while it still has one.
    Spot::Rigidbody* GetHeldBody() const;
    void SetHandsBusy(bool busy);

    Spot::EntityId m_held = Spot::EntityId::Invalid;
    // Its rotation relative to where the pawn faces, so it turns with the pawn.
    Spot::Quaternion m_rotation;
    InputContext m_carry{{.name = "Carry", .priority = 30, .actions = {"Fire", "Aim", "Use"}}};
    // While the turn action is held, the mouse turns the body rather than the view.
    InputContext m_turn{{.name = "Turn Prop", .priority = 30, .actions = {"Look"}}};
    float m_lookSensitivity = 0.0025f;
    Spot::SignalConnection m_states;
};

} // namespace Gameplay
