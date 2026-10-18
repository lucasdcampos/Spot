#pragma once

#include "graphics/render_world.h"
#include "scene/entity.h"
#include "scene/scene_system.h"

#include <cstdint>
#include <vector>

namespace Spot {

class MeshRenderer;

// The scene's MeshRenderers as a RenderWorld, which DrawScene() draws; it is created the first time the scene is
// drawn. Sync() visits only the entities that moved or whose renderers changed (Scene::TakeChangedEntities()), and
// every renderer only when what the scene holds changed (Scene::GetStructureVersion()), when it also removes the
// objects of renderers that are gone or no longer active.
class RenderWorldSystem final : public SceneSystem {
public:
    RenderWorldSystem() = default;

    // Brings the world up to date with the scene; cheap to call again in the same frame, as each view does.
    const RenderWorld& Sync();
    const RenderWorld& GetWorld() const { return m_world; }

private:
    void SyncRenderer(MeshRenderer& renderer);

    RenderWorld m_world;
    std::uint64_t m_structureVersion = 0;
    bool m_synced = false;
    std::vector<EntityId> m_changed;
};

} // namespace Spot
