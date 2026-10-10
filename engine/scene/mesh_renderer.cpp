#include "scene/mesh_renderer.h"

#include "graphics/model.h"
#include "scene/archive.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/transform.h"

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
    Entity& root = scene.CreateEntity(std::move(name));
    for (const auto& part : model.GetParts()) {
        Entity& child = root.CreateChild(part.name);
        const TransformComponents placed = Decompose(part.transform);
        child.GetTransform().SetPosition(placed.translation);
        child.GetTransform().SetRotation(placed.rotation);
        child.GetTransform().SetScale(placed.scale);
        child.AddComponent<MeshRenderer>(part.mesh, part.material).SetSubmesh(part.submesh);
    }
    return root;
}

} // namespace Spot
