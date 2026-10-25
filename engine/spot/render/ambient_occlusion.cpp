#include "spot/render/ambient_occlusion.h"

#include "spot/core/debug/log.h"
#include "spot/core/debug/profiler.h"
#include "spot/core/math/matrix.h"
#include "spot/core/math/vector.h"
#include "spot/graphics/graphics_device.h"
#include "spot/graphics/render_context.h"

#include "ambient_occlusion.frag.h"
#include "ambient_occlusion_blur.frag.h"

#include <algorithm>
#include <array>
#include <initializer_list>

namespace Spot {

namespace {

// These mirror the shaders' uniforms.
struct OcclusionUniforms {
    Matrix4 inverseProjection;
    Vector4 viewSize;
    Vector4 params;
};

struct BlurUniforms {
    Vector4 stepAndSize;
    Vector4 planes;
};

} // namespace

std::unique_ptr<AmbientOcclusion> AmbientOcclusion::Create(GraphicsDevice& device) {
    auto occlusion = std::unique_ptr<AmbientOcclusion>(new AmbientOcclusion());
    occlusion->m_device = &device;
    occlusion->m_occlusionPass =
        FullscreenPass::Create(device, Detail::Shaders::AmbientOcclusionFrag, {.targetFormat = TextureFormat::R8});
    occlusion->m_blurPass =
        FullscreenPass::Create(device, Detail::Shaders::AmbientOcclusionBlurFrag, {.targetFormat = TextureFormat::R8});
    // The shaders load texels; filtering would blend depths across edges.
    occlusion->m_sampler = device.CreateSampler({.filter = TextureFilter::Nearest});
    if (!occlusion->m_occlusionPass || !occlusion->m_blurPass || !occlusion->m_sampler) {
        Log::GetEngineLogger().Error("Could not create ambient occlusion");
        return nullptr;
    }
    return occlusion;
}

const Texture* AmbientOcclusion::Render(RenderContext& context, const Texture& depth, const Camera3D& camera,
                                        const AmbientOcclusionSettings& settings) {
    const std::uint32_t width = depth.GetWidth();
    const std::uint32_t height = depth.GetHeight();
    if (width == 0 || height == 0) {
        return nullptr;
    }
    if (!m_occlusion || m_occlusion->GetWidth() != width || m_occlusion->GetHeight() != height) {
        const TextureDesc desc{.width = width, .height = height, .format = TextureFormat::R8, .renderTarget = true};
        m_occlusion = m_device->CreateTexture(desc);
        m_blurred = m_device->CreateTexture(desc);
        if (!m_occlusion || !m_blurred) {
            m_occlusion.reset();
            return nullptr;
        }
    }
    SPOT_PROFILE_SCOPE("AmbientOcclusion::Render");
    const Vector2 size{static_cast<float>(width), static_cast<float>(height)};
    const Matrix4 projection = camera.GetProjection(size);
    const float orthographic = camera.orthographic ? 1.0f : 0.0f;
    {
        auto pass = context.BeginRenderPass({.target = m_occlusion.get()});
        // The projection's vertical scale is 1 / tan(fov / 2), or 1 / the half height for orthographic cameras.
        pass.PushFragmentUniforms(
            0, OcclusionUniforms{.inverseProjection = Inverse(projection),
                                 .viewSize = {size.x, size.y, 1.0f / size.x, 1.0f / size.y},
                                 .params = {std::max(settings.radius, 0.001f), std::max(settings.intensity, 0.0f),
                                            projection.columns[1].y * size.y * 0.5f, orthographic}});
        const std::array inputs{FullscreenInput{&depth, m_sampler.get()}};
        m_occlusionPass->Draw(pass, inputs);
    }
    // Across, then down, back into the occlusion.
    const Vector4 planes{camera.nearPlane, camera.farPlane, orthographic, 0.0f};
    for (const bool across : {true, false}) {
        auto pass = context.BeginRenderPass({.target = across ? m_blurred.get() : m_occlusion.get()});
        pass.PushFragmentUniforms(
            0, BlurUniforms{.stepAndSize = {across ? 1.0f : 0.0f, across ? 0.0f : 1.0f, size.x, size.y},
                            .planes = planes});
        const std::array inputs{FullscreenInput{across ? m_occlusion.get() : m_blurred.get(), m_sampler.get()},
                                FullscreenInput{&depth, m_sampler.get()}};
        m_blurPass->Draw(pass, inputs);
    }
    return m_occlusion.get();
}

} // namespace Spot
