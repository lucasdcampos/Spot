#include "scene/mesh_renderer.h"

#include "graphics/model.h"
#include "scene/archive.h"
#include "scene/scene.h"

namespace Spot {

void MeshRenderer::Serialize(Archive& archive) {
    archive.Field("mesh", m_mesh);
    archive.Field("material", m_material);
}

Entity& InstantiateModel(Scene& scene, const Model& model, std::string name) {
    Entity& root = scene.CreateEntity(std::move(name));
    for (const auto& part : model.GetParts()) {
        root.CreateChild(part.name).AddComponent<MeshRenderer>(part.mesh, part.material);
    }
    return root;
}

} // namespace Spot
