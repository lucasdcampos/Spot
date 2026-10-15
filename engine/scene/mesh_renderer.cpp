#include "scene/mesh_renderer.h"

#include "assets/asset_archive.h"
#include "core/archive.h"
#include "scene/scene.h"

namespace Spot {

void MeshRenderer::Serialize(Archive& archive) {
    archive.Field("mesh", m_mesh);
    AssetRef<Material> assetMaterial = m_material.GetId().IsValid() ? m_material : AssetRef<Material>{};
    archive.Field("material", assetMaterial);
    bool inlineMaterial = m_material && !m_material.GetId().IsValid();
    archive.Field("inlineMaterial", inlineMaterial);
    if (assetMaterial.GetId().IsValid() && assetMaterial.GetId() != m_material.GetId()) {
        inlineMaterial = false;
    }
    if (inlineMaterial) {
        Material material = m_material ? *m_material : Material{};
        archive.Field("materialData", material);
        if (archive.IsLoading() && (!m_material || m_material.GetId().IsValid() || material != *m_material)) {
            SetMaterial(std::move(material));
        }
    } else if (archive.IsLoading() && assetMaterial != m_material) {
        SetMaterial(std::move(assetMaterial));
    }
    archive.Field("castsShadows", m_castingShadows, {.tooltip = "Whether the mesh blocks lights that cast shadows"});
    // Saved as -1 for all of the mesh, which reads better than the largest number.
    int submesh = m_submesh == Mesh::AllSubmeshes ? -1 : static_cast<int>(m_submesh);
    archive.Field("submesh", submesh,
                  {.min = -1.0f, .max = 1024.0f, .tooltip = "The submesh it draws, as models' parts do; -1 for all"});
    if (archive.IsLoading()) {
        m_submesh = submesh < 0 ? Mesh::AllSubmeshes : static_cast<std::uint32_t>(submesh);
    }
}

Entity& InstantiateModel(Scene& scene, const Model& model, std::string name) {
    return scene.Instantiate(model, std::move(name));
}

} // namespace Spot
