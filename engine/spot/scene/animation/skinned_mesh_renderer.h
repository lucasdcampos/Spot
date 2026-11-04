#pragma once

#include "spot/animation/animation_data.h"
#include "spot/core/ids/asset_ref.h"
#include "spot/render/material.h"
#include "spot/render/mesh.h"
#include "spot/scene/component.h"
#include "spot/scene/entity.h"

#include <span>
#include <vector>

namespace Spot {

class SkinnedMeshRenderer final : public Component {
public:
    const AssetRef<Mesh>& GetMesh() const { return m_mesh; }
    void SetMesh(AssetRef<Mesh> mesh) { m_mesh = std::move(mesh); }
    const AssetRef<Material>& GetMaterial() const { return m_material; }
    void SetMaterial(AssetRef<Material> material) { m_material = std::move(material); }
    std::uint32_t GetSubmesh() const { return m_submesh; }
    void SetSubmesh(std::uint32_t submesh) { m_submesh = submesh; }
    bool IsCastingShadows() const { return m_castingShadows; }
    void SetCastingShadows(bool value) { m_castingShadows = value; }
    float GetBoundsPadding() const { return m_boundsPadding; }
    void SetBoundsPadding(float padding);
    std::span<const Matrix4> GetPalette() const { return m_palette; }
    void SetPalette(std::span<const Matrix4> palette) { m_palette.assign(palette.begin(), palette.end()); }
    EntityId GetAnimator() const { return m_animator; }
    void SetAnimator(EntityId entity) { m_animator = entity; }
    const Matrix4& GetMeshModel() const { return m_meshModel; }
    void SetMeshModel(const Matrix4& transform) { m_meshModel = transform; }
    // The skin of its mesh where it is not that of the animator's skeleton, as for the other meshes of a model with
    // several: the same bones, with joints of its own. Empty uses the animator's.
    const AssetRef<Skeleton>& GetSkinSkeleton() const { return m_skinSkeleton; }
    void SetSkinSkeleton(AssetRef<Skeleton> skeleton) { m_skinSkeleton = std::move(skeleton); }

protected:
    void Serialize(Archive& archive) override;

private:
    AssetRef<Mesh> m_mesh;
    AssetRef<Material> m_material;
    std::uint32_t m_submesh = Mesh::AllSubmeshes;
    bool m_castingShadows = true;
    float m_boundsPadding = 1.0f;
    EntityId m_animator = EntityId::Invalid;
    Matrix4 m_meshModel;
    AssetRef<Skeleton> m_skinSkeleton;
    std::vector<Matrix4> m_palette;
};

} // namespace Spot
