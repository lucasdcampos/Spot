#pragma once
#include <spot.h>

// A screen-space silhouette of all mesh parts of the held prop.
class RemoteOutline {
public:
    void Draw(const Spot::RenderStageContext& stage, Spot::Entity* prop);

private:
    std::unique_ptr<Spot::Texture> m_mask;
    std::unique_ptr<Spot::Pipeline> m_silhouette;
    std::unique_ptr<Spot::Pipeline> m_outline;
    std::unique_ptr<Spot::Sampler> m_sampler;
    Spot::Vector2 m_size;
    Spot::TextureFormat m_format = Spot::TextureFormat::Rgba8;
    std::uint32_t m_samples = 0;
};
