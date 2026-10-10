#include "scene/mesh_renderer.h"

#include "scene/archive.h"
#include "scene/scene.h"

namespace Spot {

void MeshRenderer::Serialize(Archive& archive) {
    archive.Field("mesh", m_mesh);
    archive.Field("material", m_material);
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
