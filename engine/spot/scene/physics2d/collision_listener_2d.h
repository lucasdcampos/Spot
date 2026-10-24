#pragma once

namespace Spot {

struct Collision2D;
struct TriggerContact2D;

class CollisionListener2D {
public:
    virtual ~CollisionListener2D() = default;
    virtual void OnCollisionEnter2D(const Collision2D&) {}
    virtual void OnCollisionStay2D(const Collision2D&) {}
    virtual void OnCollisionExit2D(const Collision2D&) {}
};

class TriggerListener2D {
public:
    virtual ~TriggerListener2D() = default;
    virtual void OnTriggerEnter2D(const TriggerContact2D&) {}
    virtual void OnTriggerExit2D(const TriggerContact2D&) {}
};

} // namespace Spot
