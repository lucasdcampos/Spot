#include "scene/rendering/render_world_system.h"

#include "core/profiler.h"
#include "scene/entity.h"
#include "scene/rendering/mesh_renderer.h"
#include "scene/scene.h"

#include <unordered_set>
#include <vector>

namespace Spot {

const RenderWorld& RenderWorldSystem::Sync() {
    SPOT_PROFILE_SCOPE("RenderWorldSystem::Sync");
    std::size_t visited = 0;
    GetScene().ForEachComponent<MeshRenderer>([&](MeshRenderer& renderer) {
        ++visited;
        const Entity& entity = renderer.GetEntity();
        const Transform& transform = entity.GetTransform();
        const Mesh* mesh = renderer.m_mesh.Get();
        const Material* material = renderer.m_material.Get();
        const bool known = m_world.Contains(renderer.m_sync.object);
        if (known && renderer.m_sync.version == renderer.m_version &&
            renderer.m_sync.transform == transform.GetVersion() && renderer.m_sync.layer == entity.GetLayer() &&
            renderer.m_sync.mesh == mesh && renderer.m_sync.material == material) {
            return;
        }
        // Read before taking the version, as reading brings outdated world values up to date.
        const Matrix4& worldMatrix = transform.GetWorldMatrix();
        const RenderMeshDesc desc{.mesh = mesh,
                                  .material = material,
                                  .transform = worldMatrix,
                                  .submesh = renderer.m_submesh,
                                  .castsShadows = renderer.m_castingShadows,
                                  .layer = static_cast<std::uint32_t>(entity.GetLayer())};
        if (known) {
            if (renderer.m_sync.version == renderer.m_version && renderer.m_sync.layer == entity.GetLayer() &&
                renderer.m_sync.mesh == mesh && renderer.m_sync.material == material) {
                m_world.SetTransform(renderer.m_sync.object, worldMatrix);
            } else {
                m_world.UpdateMesh(renderer.m_sync.object, desc);
            }
        } else {
            // A submesh the mesh lacks is drawn as nothing, and warned about once rather than each frame.
            renderer.m_sync.object = m_world.AddMesh(desc);
            if (renderer.m_sync.object == RenderObjectId::Invalid) {
                renderer.m_sync.object = m_world.AddMesh({.layer = desc.layer});
            }
        }
        renderer.m_sync.version = renderer.m_version;
        renderer.m_sync.transform = transform.GetVersion();
        renderer.m_sync.layer = entity.GetLayer();
        renderer.m_sync.mesh = mesh;
        renderer.m_sync.material = material;
    });

    // Every renderer visited has an object of its own, so the world holds more only when some went away.
    if (m_world.GetMeshes().size() != visited) {
        std::unordered_set<RenderObjectId> live;
        live.reserve(visited);
        GetScene().ForEachComponent<MeshRenderer>(
            [&live](const MeshRenderer& renderer) { live.insert(renderer.m_sync.object); });
        std::vector<RenderObjectId> gone;
        for (const RenderObjectId id : m_world.GetMeshIds()) {
            if (!live.contains(id)) {
                gone.push_back(id);
            }
        }
        for (const RenderObjectId id : gone) {
            m_world.Remove(id);
        }
    }
    return m_world;
}

} // namespace Spot
