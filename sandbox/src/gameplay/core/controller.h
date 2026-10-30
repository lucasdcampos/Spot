#pragma once

#include <spot.h>

namespace Gameplay {

class Pawn;

// Controls one pawn at a time, giving it what to do. It lives on an entity of its own, so it outlives the pawns it
// possesses and stays the same from one to the next. A pawn has one controller at most: possessing a pawn another one
// controls takes it from that one.
class Controller : public Spot::Component {
public:
    // The pawn's entity: the one it possesses as it starts, then the one it possesses.
    Spot::EntityId pawn = Spot::EntityId::Invalid;

    void Possess(Pawn& pawn);
    void Unpossess();
    Pawn* GetPawn() const;
    // Whether it is a player's, local or on another machine, whom game modes give pawns and respawn; an AI's is not.
    virtual bool IsPlayer() const { return false; }

protected:
    virtual void OnPossess(Pawn& /*pawn*/) {}
    virtual void OnUnpossess(Pawn& /*pawn*/) {}

    void OnStart() override;
    void OnDestroy() override;
    void Serialize(Spot::Archive& archive) override;

private:
    bool m_possessing = false;
};

} // namespace Gameplay
