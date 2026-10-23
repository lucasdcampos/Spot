#pragma once

#include "spot/animation/animation_data.h"
#include "spot/scene/component.h"
#include "spot/scene/entity.h"

#include <span>
#include <string>
#include <vector>

namespace Spot {

class Animator;

struct RagdollBone {
    std::string bone;
    Vector3 size = {0.2f, 0.3f, 0.2f};
    Vector3 center;
    float mass = 5;
    Vector3 twistAxis = {0, 1, 0};
    float swingLimit = 0.7f;
    float twistMin = -0.5f;
    float twistMax = 0.5f;
    void Serialize(Archive& archive);
    friend bool operator==(const RagdollBone&, const RagdollBone&) = default;
};

class Ragdoll final : public Component {
public:
    bool SetBones(std::span<const RagdollBone> bones);
    std::span<const RagdollBone> GetBones() const { return m_bones; }
    bool Build();
    bool SetActive(bool active, float recoveryDuration = 0.25f);
    bool IsActive() const { return m_active; }
    std::span<const EntityId> GetBodies() const { return m_bodies; }
    bool OwnsBody(EntityId body) const;
    void AddImpulse(Vector3 impulse, Vector3 worldPosition);
    void SynchronizePose();

protected:
    void OnEnable() override;
    void OnDisable() override;
    void OnDestroy() override;
    void OnValidate() override;
    void Serialize(Archive& archive) override;

private:
    Animator* GetAnimator() const;
    bool ValidateBones() const;
    void DestroyBodies();
    void ResizePose();
    std::vector<RagdollBone> m_bones;
    std::vector<EntityId> m_bodies;
    std::vector<std::size_t> m_indices;
    std::vector<TransformComponents> m_local;
    std::vector<Matrix4> m_model;
    std::vector<RagdollBone> m_appliedBones;
    bool m_initialized = false;
    bool m_requestedActive = false;
    bool m_active = false;
    bool m_characterWasEnabled = false;
    bool m_wasPlaying = false;
};

} // namespace Spot
