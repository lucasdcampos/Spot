#pragma once

#include <spot.h>
#include <spot/scene/physics/physics_query.h>

#include <optional>
#include <string>

namespace Gameplay {

class Interactor;

// Something a pawn's Interactor can use when it looks at it or at an entity below it, such as a vehicle's seat.
class Interactable : public Spot::Component {
public:
    // What the prompt says using it does, such as "Drive".
    std::string prompt = "Use";

    virtual bool CanInteract(const Interactor& /*interactor*/) const { return true; }
    virtual std::string GetPrompt(const Interactor& /*interactor*/) const { return prompt; }
    virtual void Interact(Interactor& interactor) = 0;

protected:
    void Serialize(Spot::Archive& archive) override;
};

// A component on the interactor's own entity that uses what it looks at when no Interactable does, such as a carrier
// picking up whatever is light enough.
class InteractionHandler {
public:
    virtual ~InteractionHandler() = default;

    // The prompt for what it would do with what was hit; nothing when it would do nothing.
    virtual std::optional<std::string> GetInteraction(const Interactor& interactor,
                                                      const Spot::RaycastHit& hit) const = 0;
    virtual void Interact(Interactor& interactor, const Spot::RaycastHit& hit) = 0;
};

} // namespace Gameplay
