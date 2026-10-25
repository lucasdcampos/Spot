#pragma once

// Engine-internal: Renderer3D's shadow maps, those of the directional light's cascades and the tiles of point and
// spot lights, and the casters drawn into them.

#include "spot/render/renderer_3d_internal.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Spot::Detail {

class ShadowRenderer {
public:
    explicit ShadowRenderer(Renderer3DState& state);

    bool IsValid() const;

    // Draws the shadows of the frame's lights that cast them, as long as there is room, marking those lights' shadows
    // and filling the uniforms the lit shaders read them with, and the sky's occlusion when asked and configured. The
    // frame's commands must be sorted and their instances uploaded.
    void Draw(SelectedLights& lights, ShadowUniforms& shadows, bool skyOcclusion);
    // The shadow maps and the sky's occlusion, or a map that shadows nothing, where the lit shaders read them.
    void Bind(RenderPass& pass) const;

private:
    void DrawViews(SelectedLights& lights, ShadowUniforms& shadows, bool skyOcclusion);
    // Whether the shadow map could be created.
    bool DrawCascades(const LightData& light, ShadowUniforms& shadows);
    void DrawLocalShadows(SelectedLights& lights, ShadowUniforms& shadows);
    // The depth of the highest casters around the camera, seen from above; whether it could be created.
    bool DrawSkyOcclusion(ShadowUniforms& shadows);
    void DrawCasters(RenderPass& pass, const Matrix4& viewProjection, bool ignoreNear);
    const Pipeline* GetPipeline(const DrawCommand& command) const;
    // Binds what cutout casters sample, when it changed.
    void BindCutout(RenderPass& pass, const DrawCommand& command, const Texture*& boundTexture,
                    const Sampler*& boundSampler) const;

    Renderer3DState& m_state;
    std::unique_ptr<Pipeline> m_pipeline;
    std::unique_ptr<Pipeline> m_doubleSidedPipeline;
    std::unique_ptr<Pipeline> m_cutoutPipeline;
    std::unique_ptr<Pipeline> m_doubleSidedCutoutPipeline;
    // Single-sided ones for mirroring transforms, which cull front faces, as the mirror winds them clockwise.
    std::unique_ptr<Pipeline> m_mirroredPipeline;
    std::unique_ptr<Pipeline> m_mirroredCutoutPipeline;
    std::array<std::unique_ptr<Pipeline>, 6> m_skinnedPipelines;
    std::unique_ptr<Sampler> m_sampler;
    // Bound in place of shadow maps that are not drawn.
    std::unique_ptr<Texture> m_noShadows;
    bool m_noShadowsCleared = false;
    std::unique_ptr<Texture> m_cascadeShadows;
    std::unique_ptr<Texture> m_localShadows;
    std::unique_ptr<Texture> m_skyOcclusion;
    // Whether the sky's occlusion was drawn for the view being drawn, and so holds what the lit shaders read.
    bool m_skyOcclusionDrawn = false;
    // The commands each shadow view draws, as indices into the instances.
    std::unique_ptr<Buffer> m_casters;
    // The commands that may cast shadows, in the order that keeps those that cast the same shadow side by side, which
    // each shadow view keeps those it sees of.
    std::vector<std::uint32_t> m_order;
    // The casters of every shadow view of the frame, one view after the other, as indices into the instances.
    std::vector<std::uint32_t> m_frameCasters;
};

} // namespace Spot::Detail
