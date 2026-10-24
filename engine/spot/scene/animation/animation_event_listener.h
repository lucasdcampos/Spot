#pragma once

namespace Spot {

struct AnimationEvent;

class AnimationEventListener {
public:
    virtual ~AnimationEventListener() = default;
    virtual void OnAnimationEvent(const AnimationEvent&) {}
};

} // namespace Spot
