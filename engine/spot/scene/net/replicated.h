#pragma once

#include "spot/core/io/archive.h"

namespace Spot {

// A component with state that other machines see, on an entity with a NetworkObject or below one. The authority
// describes its state with Replicate(), saving; the others read it with the same function, loading, then
// OnReplicated() runs. Describe what others need to show, not everything a save needs, as only changes are sent:
//
//     class Door final : public Spot::Component, public Spot::Replicated {
//     public:
//         bool open = false;
//         void Replicate(Spot::Archive& archive) override { archive.Field("open", open); }
//         void OnReplicated() override { /* move the door */ }
//     };
//
// Entity references cross as the ids both machines know, for replicated entities, and as EntityId::Invalid for others.
class Replicated {
public:
    virtual ~Replicated() = default;

    virtual void Replicate(Archive& archive) = 0;
    // After new values were read, on machines without authority.
    virtual void OnReplicated() {}
    // Whether, while a client owns the object with NetworkAuthority::Owner, that client decides this part too, as how
    // the wheels of the car it drives turn: it sends the part with where the object is, and is not sent it back. The
    // other parts stay the server's to decide.
    virtual bool IsDecidedByOwner() const { return false; }
};

} // namespace Spot
