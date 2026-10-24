#pragma once

// Engine-internal: Renderer3D's sky, drawn behind the view and captured into the light it gives surfaces.

#include "spot/render/fullscreen_pass.h"
#include "spot/render/renderer_3d_internal.h"

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
};

} // namespace Spot::Detail
