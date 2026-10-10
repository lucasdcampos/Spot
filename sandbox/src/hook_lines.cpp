#include "hook_lines.h"

void HookLines::Draw(const Spot::RenderStageContext& stage, std::span<const Spot::DebugLine> lines) {
    if (lines.empty()) {
        return;
    }
    const bool depth = stage.pass.depthTarget != nullptr;
    if (!m_renderer || m_format != stage.colorFormat || m_sampleCount != stage.sampleCount || m_depth != depth) {
        m_renderer = std::make_unique<Spot::LineRenderer>(stage.device,
                                                          Spot::LineRendererConfig{.targetFormat = stage.colorFormat,
                                                                                   .sampleCount = stage.sampleCount,
                                                                                   .depthTarget = depth});
        m_format = stage.colorFormat;
        m_sampleCount = stage.sampleCount;
        m_depth = depth;
    }
    if (!m_renderer->IsValid()) {
        return;
    }
    auto pass = stage.context.BeginRenderPass(stage.pass);
    m_renderer->Begin(pass, stage.viewProjection);
    m_renderer->DrawLines(lines);
    m_renderer->End();
}
