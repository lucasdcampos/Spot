#include "spot/scene/rendering/mesh_renderer.h"

#include "spot/assets/asset_archive.h"
#include "spot/core/io/archive.h"
#include "spot/scene/scene.h"

#include <array>
#include <string_view>

namespace Spot {

void MeshRendererLod::Serialize(Archive& archive) {
    archive.Field("mesh", mesh);
    archive.Field(
        "screenSize", screenSize,
        {.min = 0.0f, .max = 1.0f, .tooltip = "Shown below this height on screen, as a fraction of the view's"});
}

void MeshRenderer::Serialize(Archive& archive) {
    if (archive.IsLoading()) {
        ++m_version;
        MarkChanged();
    }
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
    static constexpr std::array<std::string_view, 2> TextureSpaces{"World", "Object"};
    archive.Field("textureSpace", m_textureSpace,
                  {.enumNames = TextureSpaces,
                   .tooltip = "Where a material's projected textures come from: the world, so blocks continue each "
                              "other's pattern, or the entity, so they move with it"});
    // Saved as -1 for all of the mesh, which reads better than the largest number.
    int submesh = m_submesh == Mesh::AllSubmeshes ? -1 : static_cast<int>(m_submesh);
    archive.Field("submesh", submesh,
                  {.min = -1.0f, .max = 1024.0f, .tooltip = "The submesh it draws, as models' parts do; -1 for all"});
    if (archive.IsLoading()) {
        m_submesh = submesh < 0 ? Mesh::AllSubmeshes : static_cast<std::uint32_t>(submesh);
    }
    archive.Field("lods", m_lods, {.tooltip = "Simpler meshes shown once the mesh looks small, most detailed first"});
    archive.Field("cullScreenSize", m_cullScreenSize,
                  {.min = 0.0f, .max = 1.0f, .tooltip = "Not drawn below this height on screen; 0 never culls"});
}

Entity& InstantiateModel(Scene& scene, const Model& model, std::string name) {
    return scene.Instantiate(model, std::move(name));
}

} // namespace Spot
