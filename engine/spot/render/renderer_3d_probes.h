#pragma once

// Engine-internal: Renderer3D's reflection probes, captured into the light they give surfaces, and kept from frame to
// frame in an atlas.

#include "spot/render/fullscreen_pass.h"
#include "spot/render/renderer_3d_internal.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Spot::Detail {

// The mip levels of the probes' reflections, from a roughness of 0 to 1. Must match PROBE_LEVELS in
// surface_lighting.hlsl.
inline constexpr std::uint32_t ProbeLevels = 5;
// The size of the faces probes capture, and of the first level of their reflections.
inline constexpr std::uint32_t ProbeCaptureSize = 128;

class ProbeRenderer {
public:
    explicit ProbeRenderer(Renderer3DState& state);

    bool IsValid() const;

    // Keeps the frame's probes, the nearest the camera first, up to Renderer3D::MaxReflectionProbes, each with its
    // tile of the atlas, which keeps the captures of probes drawn in the frames before.
    void Select(const std::vector<ReflectionProbeData>& probes);
    // The probe to capture in this frame, if any: the nearest the camera of those never captured, or else of those
    // that moved or changed version since.
    std::optional<ReflectionProbeData> GetPendingCapture() const;
    // A camera at the probe, looking along the face of a cube around it, as the main camera would see.
    static Camera3D GetFaceCamera(const ReflectionProbeData& probe, std::uint32_t face, const Camera3D& camera);
    // The pass a face of a capture draws in, as Renderer3D's pipelines draw: null when it cannot be made.
    std::optional<RenderPassDesc> GetFaceTarget();
    // Copies the face just drawn into its face of the capture.
    void FinishFace(std::uint32_t face);
    // Once the six faces are drawn, blurs the capture into the probe's tile for each roughness and projects its
    // irradiance, so the probe lights the frame from now on.
    void FinishCapture(const ReflectionProbeData& probe);
    // Sends the captured probes of the frame to the GPU, the smallest box first.
    bool Upload();
    // The probes Upload() sent.
    std::uint32_t GetCount() const { return m_count; }
    // The size of a tile's first level, the tiles across the atlas and the inverse of its size, as the lit shaders
    // take them.
    Vector4 GetAtlasUniforms() const;
    // The probes' light where the lit shaders read it, or none, for views that probes do not light.
    void Bind(RenderPass& pass, bool lit) const;

private:
    // A probe drawn in a frame, kept while its tile is.
    struct Entry {
        std::uint64_t id = 0;
        std::uint32_t tile = 0;
        std::uint64_t lastDrawn = 0;
        // What it was captured as; an entry not captured yet lights nothing.
        bool captured = false;
        Vector3 capturedPosition = {};
        std::uint32_t capturedVersion = 0;
    };

    Entry* FindEntry(std::uint64_t id);
    const Entry* FindEntry(std::uint64_t id) const;
    // The entry of a probe new to the cache, in a free tile or in place of the probe drawn longest ago; null when
    // every tile is taken by a probe of this frame.
    Entry* AddEntry(std::uint64_t id);
    bool CreateCaptureTargets();

    Renderer3DState& m_state;
    std::unique_ptr<FullscreenPass> m_facePass;
    std::unique_ptr<FullscreenPass> m_prefilterPass;
    std::unique_ptr<FullscreenPass> m_irradiancePass;
    // The faces as drawn, multisampled when Renderer3D's pipelines are, and resolved; the six faces copied into a cube
    // with mipmaps, which the reflections are blurred from.
    std::unique_ptr<Texture> m_faceColor;
    std::unique_ptr<Texture> m_faceResolved;
    std::unique_ptr<Texture> m_faceDepth;
    std::unique_ptr<Texture> m_environment;
    // The probes' reflections, a tile each, and their irradiance, a row each; black ones stand in without probes.
    std::unique_ptr<Texture> m_atlas;
    std::unique_ptr<Texture> m_irradiance;
    std::unique_ptr<Texture> m_noAtlas;
    std::unique_ptr<Buffer> m_buffer;
    std::unique_ptr<Buffer> m_noProbes;
    std::vector<Entry> m_entries;
    // The frame's probes, in Select()'s order.
    std::vector<ReflectionProbeData> m_probes;
    std::uint32_t m_count = 0;
    bool m_warnedAboutProbes = false;
};

} // namespace Spot::Detail
