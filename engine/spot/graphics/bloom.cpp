#include "spot/graphics/bloom.h"

#include "spot/core/log.h"
#include "spot/core/profiler.h"
#include "spot/graphics/graphics_device.h"
#include "spot/graphics/render_context.h"
#include "spot/math/vector.h"

#include "bloom_down.frag.h"
#include "bloom_up.frag.h"

#include <algorithm>
#include <array>

namespace Spot {

std::unique_ptr<Bloom> Bloom::Create(GraphicsDevice& device, TextureFormat format) {
    auto bloom = std::unique_ptr<Bloom>(new Bloom());
    bloom->m_device = &device;
    bloom->m_format = format;
    bloom->m_down = FullscreenPass::Create(device, Detail::Shaders::BloomDownFrag, {.targetFormat = format});
    bloom->m_up = FullscreenPass::Create(device, Detail::Shaders::BloomUpFrag, {.targetFormat = format});
    bloom->m_sampler = device.CreateSampler();
    if (!bloom->m_down || !bloom->m_up || !bloom->m_sampler) {
        Log::GetEngineLogger().Error("Could not create bloom");
        return nullptr;
    }
    return bloom;
}

const Texture* Bloom::Render(RenderContext& context, const Texture& image, const BloomSettings& settings) {
    if (settings.intensity <= 0.0f || !UpdateLevels(image.GetWidth(), image.GetHeight())) {
        return nullptr;
    }
    SPOT_PROFILE_SCOPE("Bloom::Render");
    const auto texelSize = [](const Texture& texture) {
        return Vector2{1.0f / static_cast<float>(texture.GetWidth()), 1.0f / static_cast<float>(texture.GetHeight())};
    };

    // Down: each level halves the one above, the first keeping only light above the threshold.
    const Texture* source = &image;
    const float threshold = std::max(settings.threshold, 0.0f);
    for (std::size_t i = 0; i < m_downLevels.size(); ++i) {
        auto pass = context.BeginRenderPass({.target = m_downLevels[i].get()});
        const Vector2 texel = texelSize(*source);
        pass.PushFragmentUniforms(0, i == 0 ? Vector4{texel.x, texel.y, threshold, threshold * 0.5f}
                                            : Vector4{texel.x, texel.y, 0.0f, 0.0f});
        const std::array inputs{FullscreenInput{source, m_sampler.get()}};
        m_down->Draw(pass, inputs);
        source = m_downLevels[i].get();
    }

    // Up: from the smallest level, each spreads the one below over its own.
    const float scatter = std::clamp(settings.scatter, 0.0f, 1.0f);
    const Texture* below = m_downLevels.back().get();
    for (std::size_t i = m_downLevels.size() - 1; i-- > 0;) {
        auto pass = context.BeginRenderPass({.target = m_upLevels[i].get()});
        const Vector2 texel = texelSize(*below);
        pass.PushFragmentUniforms(0, Vector4{texel.x, texel.y, scatter, 0.0f});
        const std::array inputs{FullscreenInput{m_downLevels[i].get(), m_sampler.get()},
                                FullscreenInput{below, m_sampler.get()}};
        m_up->Draw(pass, inputs);
        below = m_upLevels[i].get();
    }
    return below;
}

bool Bloom::UpdateLevels(std::uint32_t width, std::uint32_t height) {
    if (width == m_width && height == m_height && !m_downLevels.empty()) {
        return true;
    }
    m_downLevels.clear();
    m_upLevels.clear();
    m_width = width;
    m_height = height;
    // Down to a few pixels, past which the glow is as wide as it gets.
    std::uint32_t levelWidth = width / 2;
    std::uint32_t levelHeight = height / 2;
    while (m_downLevels.size() < MaxLevels && levelWidth >= 2 && levelHeight >= 2) {
        const TextureDesc desc{.width = levelWidth, .height = levelHeight, .format = m_format, .renderTarget = true};
        auto down = m_device->CreateTexture(desc);
        auto up = m_device->CreateTexture(desc);
        if (!down || !up) {
            m_downLevels.clear();
            m_upLevels.clear();
            return false;
        }
        m_downLevels.push_back(std::move(down));
        m_upLevels.push_back(std::move(up));
        levelWidth /= 2;
        levelHeight /= 2;
    }
    // An image too small to halve has no glow, and a single level is its own.
    if (m_downLevels.size() < 2) {
        m_downLevels.clear();
        m_upLevels.clear();
        return false;
    }
    return true;
}

} // namespace Spot
