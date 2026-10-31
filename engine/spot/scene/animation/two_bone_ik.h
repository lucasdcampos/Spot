#pragma once

#include "spot/core/math/matrix.h"
#include "spot/scene/component.h"
#include "spot/scene/entity.h"

#include <string>

namespace Spot {

class Animator;

// Makes a bone and its two parents, such as a hand and its arm, reach a target entity after animation, as hands
// hold what they carry or feet stand on uneven ground. The animator's AimIK constraints turn the pose first.
class TwoBoneIK final : public Component {
public:
    EntityId GetAnimator() const { return m_animator; }
    void SetAnimator(EntityId animator) { m_animator = animator; }
    // The end of the chain; its parent bends, as the elbow does.
    const std::string& GetBone() const { return m_bone; }
    void SetBone(std::string bone) { m_bone = std::move(bone); }
    EntityId GetTarget() const { return m_target; }
    void SetTarget(EntityId target) { m_target = target; }
    // Where the bone goes in the target's space.
    const TransformComponents& GetOffset() const { return m_offset; }
    void SetOffset(const TransformComponents& offset) { m_offset = offset; }
    // Whether the bone also takes the target's rotation, or keeps the animation's.
    bool IsMatchingRotation() const { return m_matchRotation; }
    void SetMatchingRotation(bool match) { m_matchRotation = match; }
    // Where the middle joint points, in the space of the bone above the chain, as the chest for an arm; zero keeps
    // the animation's bend.
    Vector3 GetPole() const { return m_pole; }
    void SetPole(Vector3 pole) { m_pole = pole; }
    float GetWeight() const { return m_weight; }
    void SetWeight(float weight) { m_weight = weight; }

    // The animator it changes: the one set, or else the nearest in its parents.
    Animator* FindAnimator() const;
    // Changes the animator's evaluated pose; the animation system does it after each animation update.
    bool Apply(Animator& animator) const;

protected:
    void OnEnable() override;
    void Serialize(Archive& archive) override;

private:
    EntityId m_animator = EntityId::Invalid;
    std::string m_bone;
    EntityId m_target = EntityId::Invalid;
    TransformComponents m_offset;
    bool m_matchRotation = true;
    Vector3 m_pole;
    float m_weight = 1.0f;
};

} // namespace Spot
