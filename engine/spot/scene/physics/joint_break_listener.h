#pragma once

namespace Spot {

class Joint;

class JointBreakListener {
public:
    virtual ~JointBreakListener() = default;
    virtual void OnJointBreak(const Joint&) {}
};

} // namespace Spot
