#pragma once

#include "spot/core/math/matrix.h"
#include "spot/scene/component.h"
#include "spot/scene/entity.h"

#include <string>

namespace Spot {

class BoneAttachment final : public Component {
public:
    EntityId GetAnimator() const { return m_animator; }
    void SetAnimator(EntityId animator) {
        m_animator = animator;
        m_warned = false;
    }
    const std::string& GetBone() const { return m_bone; }
    void SetBone(std::string bone) {
        m_bone = std::move(bone);
        m_boneIndex = -1;
        m_warned = false;
    }
    void SetBoneIndex(int index) {
        m_boneIndex = index;
        m_warned = false;
    }
    const TransformComponents& GetOffset() const { return m_offset; }
    void SetOffset(const TransformComponents& offset) { m_offset = offset; }
    bool Synchronize();

protected:
    void OnEnable() override;
    void OnDisable() override;
    void OnDestroy() override;
    void OnValidate() override { m_warned = false; }
    void Serialize(Archive& archive) override;

private:
    EntityId m_animator = EntityId::Invalid;
    std::string m_bone;
    int m_boneIndex = -1;
    TransformComponents m_offset;
    bool m_warned = false;
};

} // namespace Spot
