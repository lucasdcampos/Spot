#include "spot/scene/rendering/render_hook_system.h"
#include "spot/core/debug/log.h"
#include <algorithm>
#include <exception>
namespace Spot {
RenderHookId RenderHookSystem::AddRenderHook(RenderStage stage, RenderHook hook) {
    const auto id = static_cast<RenderHookId>(m_nextRenderHook++);
    m_renderHooks.push_back({id, stage, std::move(hook)});
    return id;
}

void RenderHookSystem::RemoveRenderHook(RenderHookId id) {
    std::erase_if(m_renderHooks, [id](const RenderHookEntry& entry) { return entry.id == id; });
}

void RenderHookSystem::RunRenderHooks(const RenderStageContext& context) {
    std::vector<RenderHookId> pending;
    for (const auto& entry : m_renderHooks) {
        if (entry.stage == context.stage)
            pending.push_back(entry.id);
    }
    for (RenderHookId id : pending) {
        const auto found = std::ranges::find(m_renderHooks, id, &RenderHookEntry::id);
        if (found == m_renderHooks.end())
            continue;
        try {
            const RenderHook hook = found->hook;
            hook(context);
        } catch (const std::exception& error) {
            Log::GetEngineLogger().Error("A render hook threw and was removed: {}", error.what());
            RemoveRenderHook(id);
        } catch (...) {
            Log::GetEngineLogger().Error("A render hook threw and was removed");
            RemoveRenderHook(id);
        }
    }
}

} // namespace Spot
