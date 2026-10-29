#pragma once

// Engine-internal: Renderer3D's shadow maps, those of the directional light's cascades and the tiles of point and
// spot lights, and the casters drawn into them.

#include "spot/render/renderer_3d_internal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
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
    // The casters a view sees, gathered into the frame's list after those of the views gathered before: commands
    // first, then the instances of each batch of a render world it reaches, one run per batch.
    struct ViewCasters {
        std::size_t start = 0;
        std::size_t commands = 0;
        std::size_t firstRun = 0;
        std::size_t runs = 0;
        // What they look like to a shadow, when asked for: their meshes, transforms, alpha masks and poses.
        std::uint64_t look = 0;
    };
    struct WorldRun {
        std::uint32_t command;
        std::uint32_t first;
        std::uint32_t count;
    };
    // What a tile of the atlas of point and spot light shadows holds, so a tile whose view and casters are as they
    // were when it was drawn is kept rather than drawn again: most lights, and most of what is around them, stand
    // still.
    struct TileContent {
        bool valid = false;
        Matrix4 viewProjection;
        std::uint64_t casters = 0;
    };
    // The first tile a light was given, so a light that stays keeps its tiles and what they hold.
    struct LightTiles {
        LightData light;
        std::size_t first = 0;
    };

    void DrawViews(SelectedLights& lights, ShadowUniforms& shadows, bool skyOcclusion);
    // Whether the shadow map could be created.
    bool DrawCascades(const LightData& light, ShadowUniforms& shadows);
    void DrawLocalShadows(SelectedLights& lights, ShadowUniforms& shadows);
    // Gives each light that casts its shadow a range of tiles, those it had last time where they are still free.
    void AssignTiles(SelectedLights& lights, std::span<const std::size_t> shadowed);
    // The depth of the highest casters around the camera, seen from above; whether it could be created.
    bool DrawSkyOcclusion(ShadowUniforms& shadows);
    // From the candidates, a subset of the casters in their order, such as those within a light's reach.
    ViewCasters GatherCasters(const Matrix4& viewProjection, bool ignoreNear, bool withLook,
                              std::span<const std::uint32_t> candidates);
    void DrawCasters(RenderPass& pass, const Matrix4& viewProjection, bool ignoreNear, const ViewCasters& casters);
    void DrawCasters(RenderPass& pass, const Matrix4& viewProjection, bool ignoreNear) {
        DrawCasters(pass, viewProjection, ignoreNear, GatherCasters(viewProjection, ignoreNear, false, m_order));
    }
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
    // Fills a tile with the far depth, as clearing the atlas does, for tiles drawn again among others kept.
    std::unique_ptr<Pipeline> m_clearTilePipeline;
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
    // The casters within reach of the light whose tiles are being gathered, which its views then go through instead of
    // all of them.
    std::vector<std::uint32_t> m_lightCasters;
    // The casters of every shadow view of the frame, one view after the other, as indices into the instances.
    std::vector<std::uint32_t> m_frameCasters;
    std::vector<WorldRun> m_worldRuns;
    std::array<TileContent, Renderer3D::MaxShadowTiles> m_tileContents{};
    std::vector<LightTiles> m_lightTiles;
};

} // namespace Spot::Detail
