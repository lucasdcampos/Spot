#pragma once

#include <spot.h>

#include <string>
#include <string_view>
#include <vector>

namespace Gameplay {

class Controller;
class PlayerController;

struct InputContextDesc {
    std::string name;
    // A context takes its actions from the active contexts below it.
    int priority = 0;
    std::vector<std::string> actions;
};

// The actions one part of the game reads, while it needs them, from the player of the controller it is activated on.
// An action an active context of a higher priority takes is not seen below it, so whoever has a button now is settled
// by priorities rather than by parts checking one another: while a prop is carried, the carrier's context takes Fire
// and the weapon in hand simply sees none.
//
// A press does one thing: a context deactivated keeps taking its actions until the frame ends, so the press that
// closed it reaches nothing below, and a press consumed reaches no context after, as the Use that gets into a car does
// not get out of it again. Contexts on any other controller than a PlayerController, or none, see nothing.
class InputContext {
public:
    explicit InputContext(InputContextDesc desc);
    // A copy is inactive.
    InputContext(const InputContext& other);
    InputContext& operator=(const InputContext&) = delete;
    ~InputContext();

    // Reads the player of the controller from now; activating it again on the same one does nothing.
    void Activate(Controller* controller);
    void Deactivate();
    bool IsActive() const;
    const InputContextDesc& GetDesc() const { return m_desc; }
    // Changes the actions taken, as from a field naming them.
    void SetActions(std::vector<std::string> actions);
    bool Takes(std::string_view action) const;
    // Whether the action reaches this context now, held or not: it is active, takes it and no context above does.
    bool IsAvailable(std::string_view action) const;
    // Uses up this frame's press or release of the action: no context sees it again this frame.
    void Consume(std::string_view action);

    bool IsDown(std::string_view action) const;
    bool IsPressed(std::string_view action) const;
    bool IsReleased(std::string_view action) const;
    float GetValue(std::string_view action) const;
    Spot::Vector2 GetVector2(std::string_view action) const;

private:
    friend class PlayerController;

    bool Sees(std::string_view action, bool edge) const;
    void Detach();

    InputContextDesc m_desc;
    PlayerController* m_controller = nullptr;
    bool m_closing = false;
};

} // namespace Gameplay
