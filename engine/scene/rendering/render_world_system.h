#pragma once

#include "graphics/render_world.h"
#include "scene/scene_system.h"

#include <cstdint>

namespace Spot {

// The scene's MeshRenderers as a RenderWorld, which DrawScene() draws. Sync() takes from each renderer only what
// changed since the last call, as their transforms' and their own versions tell, and removes the objects of renderers
// that are gone or no longer active; it is created the first time the scene is drawn.
class RenderWorldSystem final : public SceneSystem {
public:
    RenderWorldSystem() = default;

    // Brings the world up to date with the scene; cheap to call again in the same frame, as each view does.
    const RenderWorld& Sync();
    const RenderWorld& GetWorld() const { return m_world; }

private:
    RenderWorld m_world;
};

} // namespace Spot
