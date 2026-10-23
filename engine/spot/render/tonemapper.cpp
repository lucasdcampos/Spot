#include "spot/render/tonemapper.h"

#include "spot/core/log.h"
#include "spot/core/vector.h"
#include "spot/graphics/graphics_device.h"
#include "spot/graphics/render_context.h"

#include "tonemap.frag.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Spot {

std::unique_ptr<Tonemapper> Tonemapper::Create(GraphicsDevice& device, TextureFormat targetFormat) {
    auto tonemapper = std::unique_ptr<Tonemapper>(new Tonemapper());
    tonemapper->m_pass = FullscreenPass::Create(device, Detail::Shaders::TonemapFrag, {.targetFormat = targetFormat});
    // The HDR image is the size of the target, so each pixel reads its own texel.
    tonemapper->m_sampler = device.CreateSampler({.filter = TextureFilter::Nearest});
    // The bloom is smaller than the target, and smooth, so it is filtered.
    tonemapper->m_bloomSampler = device.CreateSampler();
    tonemapper->m_lutSampler = device.CreateSampler();
    tonemapper->m_black = device.CreateTexture({.width = 1, .height = 1, .format = TextureFormat::Rgba16Float});
    if (tonemapper->m_black) {
        device.UploadToTexture(*tonemapper->m_black, std::array<std::uint16_t, 4>{});
    }
    if (!tonemapper->m_pass || !tonemapper->m_sampler || !tonemapper->m_bloomSampler || !tonemapper->m_lutSampler ||
        !tonemapper->m_black) {
        Log::GetEngineLogger().Error("Could not create the tonemapper");
        return nullptr;
    }
    return tonemapper;
}

void Tonemapper::Draw(RenderPass& pass, const Texture& hdr, const TonemapSettings& settings,
                      const GradingSettings& grading, const Texture* bloom, float bloomIntensity) const {
    // Warmer light has more red and less blue, magenta less green; scaled back to keep the brightness.
    const float temperature = std::clamp(grading.temperature, -1.0f, 1.0f) * 0.3f;
    const float tint = std::clamp(grading.tint, -1.0f, 1.0f) * 0.3f;
    Vector3 balance{1.0f + temperature, 1.0f - tint, 1.0f - temperature};
    balance = balance / (0.2126f * balance.x + 0.7152f * balance.y + 0.0722f * balance.z);
    const Texture* lut = grading.lut && grading.lut->GetWidth() == grading.lut->GetHeight() * grading.lut->GetHeight()
                             ? grading.lut
                             : nullptr;
    struct Uniforms {
        Vector4 settings;
        Vector4 balanceAndContrast;
        Vector4 grading;
        Vector4 lutSize;
    };
    pass.PushFragmentUniforms(
        0, Uniforms{
               .settings = {std::max(settings.exposure, 0.0f), static_cast<float>(settings.op),
                            settings.dither ? 1.0f : 0.0f, bloom ? std::max(bloomIntensity, 0.0f) : 0.0f},
               .balanceAndContrast = {balance.x, balance.y, balance.z, std::max(grading.contrast, 0.0f)},
               .grading = {std::max(grading.saturation, 0.0f), std::clamp(grading.vignette, 0.0f, 1.0f),
                           std::clamp(grading.vignetteStart, 0.0f, 1.0f),
                           lut ? std::clamp(grading.lutContribution, 0.0f, 1.0f) : 0.0f},
               .lutSize = {lut ? static_cast<float>(lut->GetHeight()) : 1.0f, 0.0f, 0.0f, 0.0f},
           });
    const std::array inputs{FullscreenInput{&hdr, m_sampler.get()},
                            FullscreenInput{bloom ? bloom : m_black.get(), m_bloomSampler.get()},
                            FullscreenInput{lut ? lut : m_black.get(), m_lutSampler.get()}};
    m_pass->Draw(pass, inputs);
}

Image MakeNeutralLut(std::uint32_t size) {
    size = std::max(size, 2u);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * size * 4);
    const auto level = [size](std::uint32_t i) { return static_cast<std::uint8_t>(i * 255 / (size - 1)); };
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size * size; ++x) {
            std::uint8_t* pixel = &pixels[(static_cast<std::size_t>(y) * size * size + x) * 4];
            pixel[0] = level(x % size);
            pixel[1] = level(y);
            pixel[2] = level(x / size);
            pixel[3] = 255;
        }
    }
    return Image(size * size, size, std::move(pixels));
}

} // namespace Spot
