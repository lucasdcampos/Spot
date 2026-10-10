#pragma once

#include <spot.h>

#include <cstdint>
#include <memory>
#include <span>

// Draws lines from a render hook, through a LineRenderer made for the targets of the stage it draws in, again when
// they change, as the game's views and the editor's may.
class HookLines {
public:
    void Draw(const Spot::RenderStageContext& stage, std::span<const Spot::DebugLine> lines);

private:
    std::unique_ptr<Spot::LineRenderer> m_renderer;
    Spot::TextureFormat m_format = Spot::TextureFormat::Rgba8;
    std::uint32_t m_sampleCount = 0;
    bool m_depth = false;
};
