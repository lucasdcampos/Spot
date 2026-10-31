#pragma once

#include "spot/core/math/angles.h"
#include "spot/core/math/vector.h"
#include "spot/scene/component.h"
#include "spot/scene/entity.h"

#include <span>
#include <string>
#include <vector>

namespace Spot {

class Animator;

struct AimIKBone {
    std::string bone;
    // The share of the turn still left that the bone takes; the last bone's 1 aims exactly.
    float weight = 1.0f;
    void Serialize(Archive& archive);
};

// Turns a chain of bones after animation so the last one points at a target, as a spine aims what the arms hold or
// a neck turns the head to look. Constraints of one animator turn parents first, then TwoBoneIK ones reach.
class AimIK final : public Component {
public:
    EntityId GetAnimator() const { return m_animator; }
    void SetAnimator(EntityId animator) { m_animator = animator; }
    // Parents first.
    std::span<const AimIKBone> GetBones() const { return m_bones; }
    void SetBones(std::vector<AimIKBone> bones) { m_bones = std::move(bones); }
    // The entity it aims at; without one, the target position.
    EntityId GetTarget() const { return m_target; }
    void SetTarget(EntityId target) { m_target = target; }
    // In world space.
    Vector3 GetTargetPosition() const { return m_targetPosition; }
    void SetTargetPosition(Vector3 position) { m_targetPosition = position; }
    // The direction the last bone aims, in its own space; -Z, forward, by default.
    Vector3 GetAxis() const { return m_axis; }
    void SetAxis(Vector3 axis) { m_axis = axis; }
    // How far, in radians, the chain turns away from the animation at most.
    float GetMaxAngle() const { return m_maxAngle; }
    void SetMaxAngle(float radians) { m_maxAngle = radians; }
    float GetWeight() const { return m_weight; }
    void SetWeight(float weight) { m_weight = weight; }

    // The animator it changes: the one set, or else the nearest in its parents.
    Animator* FindAnimator() const;
    // The skeleton index of its first bone, which orders constraints parents first; the bone count when unknown.
    std::size_t GetFirstBoneIndex(const Animator& animator) const;
    // Changes the animator's evaluated pose; the animation system does it after each animation update.
    bool Apply(Animator& animator) const;

protected:
    void OnEnable() override;
    void Serialize(Archive& archive) override;

private:
    EntityId m_animator = EntityId::Invalid;
    std::vector<AimIKBone> m_bones;
    EntityId m_target = EntityId::Invalid;
    Vector3 m_targetPosition;
    Vector3 m_axis{0.0f, 0.0f, -1.0f};
    float m_maxAngle = Pi;
    float m_weight = 1.0f;
};

} // namespace Spot
