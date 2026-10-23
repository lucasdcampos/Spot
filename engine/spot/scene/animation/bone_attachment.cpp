#include "spot/scene/animation/bone_attachment.h"

#include "spot/assets/asset_archive.h"
#include "spot/core/archive.h"
#include "spot/core/log.h"
#include "spot/scene/animation/animator.h"
#include "spot/scene/scene.h"

namespace Spot {

bool BoneAttachment::Synchronize() {
    auto* owner = GetScene().GetEntity(m_animator);
    auto* animator = owner ? owner->GetComponent<Animator>() : nullptr;
    if (!animator && m_animator == EntityId::Invalid && GetEntity().GetParent()) {
        animator = GetEntity().GetParent()->GetComponentInParent<Animator>();
        owner = animator ? &animator->GetEntity() : nullptr;
    }
    bool cycle = false;
    for (const auto* ancestor = owner; ancestor; ancestor = ancestor->GetParent()) {
        cycle = cycle || ancestor == &GetEntity();
    }
    if (animator && animator->GetSkeleton() && !cycle) {
        const auto& skeleton = *animator->GetSkeleton();
        const auto fallback = skeleton.joints.empty() ? 0u : skeleton.joints.front();
        const auto bone = m_boneIndex >= 0 ? static_cast<std::size_t>(m_boneIndex)
                          : m_bone.empty() ? static_cast<std::size_t>(fallback)
                                           : skeleton.FindBone(m_bone).value_or(skeleton.bones.size());
        if (bone < animator->GetModelPose().size()) {
            const auto offset = Matrix4::Translation(m_offset.translation) * Matrix4::Rotation(m_offset.rotation) *
                                Matrix4::Scale(m_offset.scale);
            GetEntity().GetTransform().SetWorldOverride(owner->GetTransform().GetWorldMatrix() *
                                                        animator->GetModelPose()[bone] * offset);
            m_warned = false;
            return true;
        }
    }
    GetEntity().GetTransform().ClearWorldOverride();
    if (!m_warned) {
        Log::GetEngineLogger().Warning(
            "Bone attachment '{}' could not resolve its animator/bone '{}', or forms a cycle", GetEntity().GetName(),
            m_bone);
        m_warned = true;
    }
    return false;
}

void BoneAttachment::OnEnable() {
    if (!GetScene().GetSystem<AnimationSystem>()) {
        GetScene().AddSystem<AnimationSystem>();
    }
}
void BoneAttachment::OnDisable() {
    GetEntity().GetTransform().ClearWorldOverride();
}
void BoneAttachment::OnDestroy() {
    GetEntity().GetTransform().ClearWorldOverride();
}

void BoneAttachment::Serialize(Archive& archive) {
    archive.Field("animator", m_animator, {.componentType = "Animator"});
    archive.Field("bone", m_bone);
    archive.Field("boneIndex", m_boneIndex, {.hidden = true});
    archive.Field("offsetPosition", m_offset.translation);
    archive.Field("offsetRotation", m_offset.rotation);
    archive.Field("offsetScale", m_offset.scale);
}

} // namespace Spot
