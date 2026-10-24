#pragma once
#include "spot/render/render_stage.h"
#include "spot/scene/scene_system.h"
#include <vector>
namespace Spot {
class RenderHookSystem final : public SceneSystem {
public:
    RenderHookId AddRenderHook(RenderStage stage, RenderHook hook);
    void RemoveRenderHook(RenderHookId id);
    void RunRenderHooks(const RenderStageContext& context);

private:
    struct RenderHookEntry {
        RenderHookId id;
        RenderStage stage;
        RenderHook hook;
    };
    std::vector<RenderHookEntry> m_renderHooks;
    std::uint32_t m_nextRenderHook = 1;
};
} // namespace Spot
