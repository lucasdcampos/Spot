#include "spot/scene/animation/two_bone_ik.h"

#include "spot/animation/inverse_kinematics.h"
#include "spot/core/io/archive.h"
#include "spot/scene/animation/animator.h"
#include "spot/scene/scene.h"

namespace Spot {

Animator* TwoBoneIK::FindAnimator() const {
    if (m_animator == EntityId::Invalid) {
        return GetEntity().GetComponentInParent<Animator>();
    }
    auto* owner = GetScene().GetEntity(m_animator);
    return owner ? owner->GetComponent<Animator>() : nullptr;
}

bool TwoBoneIK::Apply(Animator& animator) const {
    const auto* target = GetScene().GetEntity(m_target);
    const auto& skeleton = animator.GetSkeleton();
    const auto bone = skeleton ? skeleton->FindBone(m_bone) : std::nullopt;
    if (!target || !bone || m_weight <= 0) {
        return false;
    }
    const auto goal = Decompose(Inverse(animator.GetEntity().GetTransform().GetWorldMatrix()) *
                                target->GetTransform().GetWorldMatrix() * Matrix4::Translation(m_offset.translation) *
                                Matrix4::Rotation(m_offset.rotation));
    bool solved = false;
    animator.EditPose([&](const Skeleton& rig, std::span<TransformComponents> local, std::span<Matrix4> model) {
        Pose::TwoBoneGoal twoBone{.position = goal.translation, .weight = m_weight};
        if (m_matchRotation) {
            twoBone.rotation = goal.rotation;
        }
        // The pole turns with the bone above the chain, as elbows do with the chest.
        int above = rig.bones[*bone].parent;
        for (int i = 0; i < 2 && above >= 0; ++i) {
            above = rig.bones[static_cast<std::size_t>(above)].parent;
        }
        twoBone.pole = above >= 0 ? Decompose(model[static_cast<std::size_t>(above)]).rotation * m_pole : m_pole;
        solved = Pose::SolveTwoBone(rig, *bone, twoBone, local, model);
    });
    return solved;
}

void TwoBoneIK::OnEnable() {
    if (!GetScene().GetSystem<AnimationSystem>()) {
        GetScene().AddSystem<AnimationSystem>();
    }
}

void TwoBoneIK::Serialize(Archive& archive) {
    archive.Field("animator", m_animator, {.componentType = "Animator"});
    archive.Field("bone", m_bone, {.tooltip = "The end of the chain, such as a hand; its parent bends"});
    archive.Field("target", m_target);
    archive.Field("offsetPosition", m_offset.translation);
    archive.Field("offsetRotation", m_offset.rotation);
    archive.Field("matchRotation", m_matchRotation);
    archive.Field("pole", m_pole,
                  {.tooltip = "Where the middle joint points, in the space of the bone above the chain; zero keeps "
                              "the animation's bend"});
    archive.Field("weight", m_weight, {.min = 0, .max = 1});
}

} // namespace Spot
