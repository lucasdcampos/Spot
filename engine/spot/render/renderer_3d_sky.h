#pragma once

// Engine-internal: Renderer3D's sky, drawn behind the view and captured into the light it gives surfaces, and its
// clouds, drawn over the view's opaque surfaces.

#include "spot/render/fullscreen_pass.h"
#include "spot/render/renderer_3d_internal.h"

#include <array>
#include <memory>
#include <optional>

namespace Spot::Detail {

class SkyRenderer {
public:
    explicit SkyRenderer(Renderer3DState& state);

    bool IsValid() const;

    // Captures the sky's light into the reflections and irradiance when it changed; false when it cannot. Surfaces
    // are then lit by it until the next call.
    bool Update(const SkyData& sky, const SkyLighting& lighting);
    void SetLit(bool lit) { m_lit = lit; }
    bool IsLit() const { return m_lit; }
    // Fills the view behind everything with the sky.
    void Draw(RenderPass& pass, const SkyData& sky, const SkyLighting& lighting, const Matrix4& inverseViewProjection);
    // Whether the sky has clouds to draw, making what they need the first time; called before the view's pass opens.
    bool PrepareClouds(const SkyData& sky);
    // Draws the clouds over the opaque surfaces drawn so far, where each pixel's ray meets them first, from the camera
    // of the frame. The matrices are of its rotation and projection only, as the sky's.
    void DrawClouds(RenderPass& pass, const SkyData& sky, const SkyLighting& lighting, const Matrix4& viewProjection,
                    const Matrix4& inverseViewProjection);
    // Whether the main view draws the clouds as a volume (Renderer3DConfig::volumetricClouds, into HDR targets).
    bool IsVolumetric() const { return m_volumetric; }
    // Marches the clouds' volume at half the size of the camera's depth, against it, and blends what it finds with the
    // frames before; called before the view's pass opens, which CompositeClouds() then draws them into.
    void RenderVolumetricClouds(const SkyData& sky, const SkyLighting& lighting, const Texture& cameraDepth,
                                const Matrix4& viewProjection, const Matrix4& inverseViewProjection);
    void CompositeClouds(RenderPass& pass, const Texture& cameraDepth);
    // The captured light, or black, where the lit shaders read it.
    void Bind(RenderPass& pass) const;

    // The mip levels of the reflections, from a roughness of 0 to 1.
    static constexpr std::uint32_t ReflectionLevels = 5;

private:
    Renderer3DState& m_state;
    std::unique_ptr<Pipeline> m_pipeline;
    // The sky's light, as surfaces show it: captured into a cube, then blurred for each roughness down the mip levels
    // of the reflections, and projected onto spherical harmonics in the irradiance. Black ones stand in without a
    // sky.
    std::unique_ptr<Pipeline> m_capturePipeline;
    std::unique_ptr<FullscreenPass> m_prefilter;
    std::unique_ptr<FullscreenPass> m_irradiancePass;
    std::unique_ptr<Sampler> m_sampler;
    std::unique_ptr<Texture> m_environment;
    std::unique_ptr<Texture> m_reflections;
    std::unique_ptr<Texture> m_irradiance;
    std::unique_ptr<Texture> m_noReflections;
    std::unique_ptr<Texture> m_noIrradiance;
    std::optional<SkyData> m_capturedSky;
    bool m_lit = false;
    std::unique_ptr<Pipeline> m_cloudPipeline;
    // The noise the clouds are shaped from (cloud_noise.h), made when a sky first has clouds, and its repeating
    // sampler.
    std::unique_ptr<Texture> m_cloudNoise;
    std::unique_ptr<Sampler> m_cloudSampler;

    // The volume: its noise, the passes that march it and blend it with the frames before, at half the view's size,
    // and the pipeline that draws it into the view. The last two images take turns holding what is shown.
    bool m_volumetric = false;
    std::unique_ptr<Texture> m_volumeNoise;
    std::unique_ptr<FullscreenPass> m_marchPass;
    std::unique_ptr<FullscreenPass> m_resolvePass;
    std::unique_ptr<Pipeline> m_compositePipeline;
    std::unique_ptr<Texture> m_marched;
    std::array<std::unique_ptr<Texture>, 2> m_resolved;
    std::uint32_t m_shown = 0;
    // What the last frame's clouds were seen from, to find where they were then.
    bool m_hasHistory = false;
    Matrix4 m_previousViewProjection;
    Vector3 m_previousCamera;
    Vector2 m_previousOffset;
    std::uint32_t m_cloudFrame = 0;
    // As clouds_volume.hlsl declares them.
    struct FrameUniforms {
        Matrix4 inverseViewProjection;
        Matrix4 previousViewProjection;
        Vector4 reprojection;
        Vector4 layer;
        Vector4 cloudSize;
        Vector4 viewSize;
    };
    FrameUniforms m_frame{};
};

} // namespace Spot::Detail
