#include "scene/rendering/render_world_system.h"

#include "core/profiler.h"
#include "scene/entity.h"
#include "scene/rendering/mesh_renderer.h"
#include "scene/scene.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace Spot {

void RenderWorldSystem::SyncRenderer(MeshRenderer& renderer) {
    const Entity& entity = renderer.GetEntity();
    const Transform& transform = entity.GetTransform();
    const Mesh* mesh = renderer.m_mesh.Get();
    const Material* material = renderer.m_material.Get();
    const bool known = m_world.Contains(renderer.m_sync.object);
    const bool sameLook = renderer.m_sync.version == renderer.m_version && renderer.m_sync.layer == entity.GetLayer() &&
                          renderer.m_sync.mesh == mesh && renderer.m_sync.material == material;
    if (known && sameLook && renderer.m_sync.transform == transform.GetVersion()) {
        return;
    }
    // Reading the world matrix brings outdated world values up to date, so the version is taken after it.
    const Matrix4& worldMatrix = transform.GetWorldMatrix();
    RenderMeshDesc desc{.mesh = mesh,
                        .material = material,
                        .transform = worldMatrix,
                        .submesh = renderer.m_submesh,
                        .castsShadows = renderer.m_castingShadows,
                        .layer = static_cast<std::uint32_t>(entity.GetLayer()),
                        .cullScreenSize = renderer.m_cullScreenSize};
    for (std::size_t i = 0; i < std::min(renderer.m_lods.size(), desc.lods.size()); ++i) {
        desc.lods[i] = {renderer.m_lods[i].mesh.Get(), renderer.m_lods[i].screenSize};
    }
    if (known && sameLook) {
        m_world.SetTransform(renderer.m_sync.object, worldMatrix);
    } else if (known) {
        m_world.UpdateMesh(renderer.m_sync.object, desc);
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
}

const RenderWorld& RenderWorldSystem::Sync() {
    SPOT_PROFILE_SCOPE("RenderWorldSystem::Sync");
    Scene& scene = GetScene();
    scene.TrackChanges();
    m_changed.clear();
    const bool complete = scene.TakeChangedEntities(m_changed);

    // Between changes to what the scene holds, only the entities that changed are visited, so a world that mostly
    // stands still costs next to nothing.
    if (complete && m_synced && scene.GetStructureVersion() == m_structureVersion) {
        for (const EntityId id : m_changed) {
            if (Entity* entity = scene.GetEntity(id)) {
                entity->ForEachComponent<MeshRenderer>([this](MeshRenderer& renderer) {
                    // Renderers without an object are not drawn, or new, which changes the version above.
                    if (m_world.Contains(renderer.m_sync.object)) {
                        SyncRenderer(renderer);
                    }
                });
            }
        }
        return m_world;
    }
    m_synced = true;
    m_structureVersion = scene.GetStructureVersion();

    std::size_t visited = 0;
    scene.ForEachComponent<MeshRenderer>([&](MeshRenderer& renderer) {
        ++visited;
        SyncRenderer(renderer);
    });
    // Every renderer visited has an object of its own, so the world holds more only when some went away.
    if (m_world.GetMeshes().size() != visited) {
        std::unordered_set<RenderObjectId> live;
        live.reserve(visited);
        scene.ForEachComponent<MeshRenderer>(
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
