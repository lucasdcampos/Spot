#pragma once

#include "gameplay/core/controller.h"

#include <string>
#include <string_view>
#include <vector>

namespace Gameplay {

class InputContext;
class PlayerInputSystem;

// The controller of a local player: the parts of the pawn it possesses, and the player's own tools, read the player's
// actions through input contexts activated on it, and it shows the pawn's camera while possessing it.
class PlayerController : public Controller {
public:
    PlayerController() = default;
    PlayerController(const PlayerController&) = delete;
    PlayerController& operator=(const PlayerController&) = delete;
    ~PlayerController() override;

    // Whether an active context above the one given takes the action.
    bool IsTakenAbove(const InputContext& context, std::string_view action) const;
    // Whether a context consumed the action's press or release this frame.
    bool IsConsumed(std::string_view action) const;
    bool IsPlayer() const override { return true; }

protected:
    void OnStart() override;
    void OnPossess(Pawn& pawn) override;
    void OnUnpossess(Pawn& pawn) override;
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void OnDestroy() override;

private:
    friend class InputContext;
    friend class PlayerInputSystem;

    void ReadInput(bool beforeFixedSteps);

    void ShowCamera(const Pawn& pawn, bool show);
    void DetachContexts();

    std::vector<InputContext*> m_contexts;
    std::vector<std::string> m_consumed;
};

} // namespace Gameplay
