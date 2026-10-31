#pragma once

#include "spot/core/math/quaternion.h"
#include "spot/core/math/vector.h"
#include "spot/scene/component.h"

#include <deque>

namespace Spot {

class BitReader;
class BitWriter;

// Sends its entity's position and rotation from the authority to the other machines, which show it a little in the
// past, between the last states they received, so it moves smoothly whatever the network does. On those machines,
// its entity's Rigidbody becomes kinematic, as the authority simulates it. The owner of a predicted object
// (NetworkObject::predicted) is not shown it: it moves the entity itself.
class NetworkTransform final : public Component {
public:
    // Positions are sent with this precision, in meters, within this distance of the origin.
    float precision = 0.001f;
    float range = 4096.0f;
    // Farther than this in one step, it jumps there instead of moving, as when it was teleported.
    float teleportDistance = 10.0f;
    // Seconds it may carry on moving past the last state when the next ones are late.
    float maxExtrapolation = 0.25f;

    void Serialize(Archive& archive) override;

    // How fast it moved between the last states it received, in meters per second; zero with fewer than two.
    Vector3 GetShownVelocity() const;

private:
    friend class NetworkSystem;

    struct Sample {
        double time = 0.0;
        Vector3 position;
        Quaternion rotation;
    };

    void Write(BitWriter& writer) const;
    // Reads a state the authority had at that time, keeping it to move between.
    void Read(BitReader& reader, double time);
    // Moves the entity where it was at that time. True when it was moving and that time is past the last state it has,
    // as when states come late: it carries on as it was going for a while, then stops until the next.
    bool ShowAt(double time);
    int GetPositionBits() const;

    std::deque<Sample> m_samples;
};

} // namespace Spot
