#include "scene/scene.h"

#include "core/profiler.h"
#include "graphics/model.h"
#include "scene/mesh_renderer.h"

#include <utility>
#include <vector>

namespace Spot {

Entity& Scene::Instantiate(const Model& model, std::string name) {
    SPOT_PROFILE_SCOPE("Scene::Instantiate(Model)");
    Entity& root = CreateEntity(std::move(name));
    std::vector<Entity*> instances;
    instances.reserve(model.GetNodes().size());
    for (const ModelNode& node : model.GetNodes()) {
        Entity& parent = node.parent ? *instances[*node.parent] : root;
        Entity& entity = parent.CreateChild(node.name);
        // Decomposing local matrices preserves the shear a rotated child under a scaled parent produces in world
        // space; decomposing the accumulated matrix would lose it.
        const TransformComponents local = Decompose(node.transform);
        entity.GetTransform().SetPosition(local.translation);
        entity.GetTransform().SetRotation(local.rotation);
        entity.GetTransform().SetScale(local.scale);
        instances.push_back(&entity);
    }
    for (const ModelPart& part : model.GetParts()) {
        instances[part.node]->AddComponent<MeshRenderer>(part.mesh, part.material).SetSubmesh(part.submesh);
    }
    return root;
}

} // namespace Spot
