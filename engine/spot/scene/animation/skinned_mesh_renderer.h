#pragma once

#include "spot/assets/asset_ref.h"
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
    std::vector<Matrix4> m_palette;
};

} // namespace Spot
