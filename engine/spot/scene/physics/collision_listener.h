#pragma once

namespace Spot {

struct Collision;
struct TriggerContact;

class CollisionListener {
public:
    virtual ~CollisionListener() = default;
    virtual void OnCollisionEnter(const Collision&) {}
    virtual void OnCollisionStay(const Collision&) {}
    virtual void OnCollisionExit(const Collision&) {}
};

class TriggerListener {
public:
    virtual ~TriggerListener() = default;
    virtual void OnTriggerEnter(const TriggerContact&) {}
    virtual void OnTriggerExit(const TriggerContact&) {}
};

} // namespace Spot
