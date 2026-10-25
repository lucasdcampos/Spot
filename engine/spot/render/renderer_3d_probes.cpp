#include "spot/render/renderer_3d_probes.h"

#include "spot/core/debug/log.h"
#include "spot/core/math/angles.h"
#include "spot/core/math/quaternion.h"

#include "probe_face.frag.h"
#include "probe_prefilter.frag.h"
#include "sky_irradiance.frag.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace Spot::Detail {

namespace {

constexpr std::uint32_t AtlasColumns = 4;
constexpr std::uint32_t AtlasRows = static_cast<std::uint32_t>(Renderer3D::MaxReflectionProbes) / AtlasColumns;
// A tile holds its first level, a square, and the others in a row under it, each half the size of the one before.
constexpr std::uint32_t TileHeight = ProbeCaptureSize + ProbeCaptureSize / 2;
// The irradiance is projected from the capture's faces at this size, as the sky's is.
constexpr std::uint32_t IrradianceSamples = 16;

// As surface_lighting.hlsl reads a probe.
struct ProbeUniforms {
    Vector4 boxMin;
    Vector4 boxMax;
    Vector4 position;
};

// How far the point is from the probe's box; 0 inside it.
float GetDistanceToBox(const ReflectionProbeData& probe, Vector3 point) {
    const float x = std::max({probe.boxMin.x - point.x, 0.0f, point.x - probe.boxMax.x});
    const float y = std::max({probe.boxMin.y - point.y, 0.0f, point.y - probe.boxMax.y});
    const float z = std::max({probe.boxMin.z - point.z, 0.0f, point.z - probe.boxMax.z});
    return std::sqrt(x * x + y * y + z * z);
}

float GetVolume(const ReflectionProbeData& probe) {
    return std::abs((probe.boxMax.x - probe.boxMin.x) * (probe.boxMax.y - probe.boxMin.y) *
                    (probe.boxMax.z - probe.boxMin.z));
}

} // namespace

ProbeRenderer::ProbeRenderer(Renderer3DState& state) : m_state(state) {
    GraphicsDevice& device = *state.device;
    m_facePass = FullscreenPass::Create(device, Shaders::ProbeFaceFrag, {.targetFormat = TextureFormat::Rgba16Float});
    m_prefilterPass =
        FullscreenPass::Create(device, Shaders::ProbePrefilterFrag, {.targetFormat = TextureFormat::Rgba16Float});
    m_irradiancePass =
        FullscreenPass::Create(device, Shaders::SkyIrradianceFrag, {.targetFormat = TextureFormat::Rgba16Float});
    // Shaders read it only for probes, so black stands in for both the atlas and the irradiance.
    m_noAtlas = device.CreateTexture({.width = 9, .height = 1, .format = TextureFormat::Rgba16Float});
    if (m_noAtlas) {
        device.UploadToTexture(*m_noAtlas, std::array<std::uint16_t, 9 * 4>{});
    }
    m_noProbes = device.CreateBuffer(BufferUsage::Storage, std::array<ProbeUniforms, 1>{});
}

bool ProbeRenderer::IsValid() const {
    return m_facePass && m_prefilterPass && m_irradiancePass && m_noAtlas && m_noProbes;
}

ProbeRenderer::Entry* ProbeRenderer::FindEntry(std::uint64_t id) {
    const auto found = std::ranges::find(m_entries, id, &Entry::id);
    return found != m_entries.end() ? &*found : nullptr;
}

const ProbeRenderer::Entry* ProbeRenderer::FindEntry(std::uint64_t id) const {
    const auto found = std::ranges::find(m_entries, id, &Entry::id);
    return found != m_entries.end() ? &*found : nullptr;
}

ProbeRenderer::Entry* ProbeRenderer::AddEntry(std::uint64_t id) {
    const std::uint64_t frame = m_state.endCount + 1;
    if (m_entries.size() < Renderer3D::MaxReflectionProbes) {
        std::uint32_t tile = 0;
        while (std::ranges::any_of(m_entries, [tile](const Entry& entry) { return entry.tile == tile; })) {
            ++tile;
        }
        m_entries.push_back({.id = id, .tile = tile, .lastDrawn = frame});
        return &m_entries.back();
    }
    Entry* oldest = nullptr;
    for (Entry& entry : m_entries) {
        if (entry.lastDrawn != frame && (!oldest || entry.lastDrawn < oldest->lastDrawn)) {
            oldest = &entry;
        }
    }
    if (oldest) {
        *oldest = {.id = id, .tile = oldest->tile, .lastDrawn = frame};
    }
    return oldest;
}

void ProbeRenderer::Select(const std::vector<ReflectionProbeData>& probes) {
    m_probes = probes;
    const Vector3 eye = m_state.camera.position;
    std::ranges::stable_sort(m_probes, {},
                             [eye](const ReflectionProbeData& probe) { return GetDistanceToBox(probe, eye); });
    if (m_probes.size() > Renderer3D::MaxReflectionProbes) {
        if (!m_warnedAboutProbes) {
            Log::GetEngineLogger().Warning(
                "Renderer3D was given {} reflection probes in a frame, more than the {} it can use; the farthest are "
                "left out",
                m_probes.size(), Renderer3D::MaxReflectionProbes);
            m_warnedAboutProbes = true;
        }
        m_probes.resize(Renderer3D::MaxReflectionProbes);
    }
    // The probes of the frame keep their tiles first, so new ones only take those of probes no longer drawn.
    const std::uint64_t frame = m_state.endCount + 1;
    for (const ReflectionProbeData& probe : m_probes) {
        if (Entry* entry = FindEntry(probe.id)) {
            entry->lastDrawn = frame;
        }
    }
    std::erase_if(m_probes,
                  [this](const ReflectionProbeData& probe) { return !FindEntry(probe.id) && !AddEntry(probe.id); });
}

std::optional<ReflectionProbeData> ProbeRenderer::GetPendingCapture() const {
    const ReflectionProbeData* changed = nullptr;
    for (const ReflectionProbeData& probe : m_probes) {
        const Entry* entry = FindEntry(probe.id);
        if (!entry) {
            continue;
        }
        if (!entry->captured) {
            return probe;
        }
        if (!changed && (entry->capturedPosition != probe.position || entry->capturedVersion != probe.version)) {
            changed = &probe;
        }
    }
    return changed ? std::optional(*changed) : std::nullopt;
}

Camera3D ProbeRenderer::GetFaceCamera(const ReflectionProbeData& probe, std::uint32_t face, const Camera3D& camera) {
    // The main camera's planes and layers, so a capture holds what the view would show.
    Camera3D faceCamera = camera;
    faceCamera.position = probe.position;
    faceCamera.rotation = Quaternion::LookRotation(CubeFaces[face][0], CubeFaces[face][2]);
    faceCamera.fieldOfView = ToRadians(90.0f);
    faceCamera.nearPlane = std::min(camera.nearPlane, 0.05f);
    faceCamera.orthographic = false;
    faceCamera.pixelPerfect = {};
    return faceCamera;
}

bool ProbeRenderer::CreateCaptureTargets() {
    if (m_faceColor && m_faceDepth && m_environment && m_atlas && m_irradiance) {
        return true;
    }
    GraphicsDevice& device = *m_state.device;
    const TextureFormat format = m_state.config.targetFormat.value_or(device.GetWindowFormat());
    const std::uint32_t samples = m_state.config.sampleCount;
    m_faceColor = device.CreateTexture({.width = ProbeCaptureSize,
                                        .height = ProbeCaptureSize,
                                        .format = format,
                                        .renderTarget = true,
                                        .sampleCount = samples});
    m_faceResolved =
        samples > 1
            ? device.CreateTexture(
                  {.width = ProbeCaptureSize, .height = ProbeCaptureSize, .format = format, .renderTarget = true})
            : nullptr;
    m_faceDepth = device.CreateTexture({.width = ProbeCaptureSize,
                                        .height = ProbeCaptureSize,
                                        .format = m_state.config.depthFormat,
                                        .renderTarget = true,
                                        .sampleCount = samples});
    m_environment = device.CreateTexture({.width = ProbeCaptureSize,
                                          .height = ProbeCaptureSize,
                                          .format = TextureFormat::Rgba16Float,
                                          .type = TextureType::Cube,
                                          .renderTarget = true,
                                          .mipmaps = true});
    m_atlas = device.CreateTexture({.width = AtlasColumns * ProbeCaptureSize,
                                    .height = AtlasRows * TileHeight,
                                    .format = TextureFormat::Rgba16Float,
                                    .renderTarget = true});
    m_irradiance = device.CreateTexture({.width = 9,
                                         .height = static_cast<std::uint32_t>(Renderer3D::MaxReflectionProbes),
                                         .format = TextureFormat::Rgba16Float,
                                         .renderTarget = true});
    if (!m_faceColor || (samples > 1 && !m_faceResolved) || !m_faceDepth || !m_environment || !m_atlas ||
        !m_irradiance) {
        m_faceColor.reset();
        m_faceResolved.reset();
        m_faceDepth.reset();
        m_environment.reset();
        m_atlas.reset();
        m_irradiance.reset();
        // The probes captured into the atlas went with it.
        m_entries.clear();
        return false;
    }
    return true;
}

std::optional<RenderPassDesc> ProbeRenderer::GetFaceTarget() {
    if (!CreateCaptureTargets()) {
        return std::nullopt;
    }
    return RenderPassDesc{.target = m_faceColor.get(),
                          .clearColor = Color::Black,
                          .depthTarget = m_faceDepth.get(),
                          .clearDepth = 1.0f,
                          .resolveTarget = m_faceResolved.get()};
}

void ProbeRenderer::FinishFace(std::uint32_t face) {
    const Texture& drawn = m_faceResolved ? *m_faceResolved : *m_faceColor;
    auto pass = m_state.context->BeginRenderPass({.target = m_environment.get(), .targetLayer = face});
    const std::array inputs{FullscreenInput{&drawn, m_state.clampSampler.get()}};
    m_facePass->Draw(pass, inputs);
}

void ProbeRenderer::FinishCapture(const ReflectionProbeData& probe) {
    Entry* entry = FindEntry(probe.id);
    if (!entry) {
        return;
    }
    RenderContext& context = *m_state.context;
    context.GenerateMipmaps(*m_environment);
    const FullscreenInput environment{m_environment.get(), m_state.clampSampler.get()};
    const float size = static_cast<float>(ProbeCaptureSize);
    const float levels = static_cast<float>(m_environment->GetMipLevelCount());
    const float tileX = static_cast<float>(entry->tile % AtlasColumns * ProbeCaptureSize);
    const float tileY = static_cast<float>(entry->tile / AtlasColumns * TileHeight);
    {
        // Into the probe's tile only, keeping the others' captures.
        auto pass = context.BeginRenderPass({.target = m_atlas.get()});
        for (std::uint32_t level = 0; level < ProbeLevels; ++level) {
            const float levelSize = static_cast<float>(ProbeCaptureSize >> level);
            pass.SetViewport({.x = tileX + (level == 0 ? 0.0f : size - 2.0f * levelSize),
                              .y = tileY + (level == 0 ? 0.0f : size),
                              .width = levelSize,
                              .height = levelSize});
            const float roughness = static_cast<float>(level) / static_cast<float>(ProbeLevels - 1);
            pass.PushFragmentUniforms(0, Vector4{roughness, size, levels, levelSize});
            m_prefilterPass->Draw(pass, {&environment, 1});
        }
    }
    {
        auto pass = context.BeginRenderPass({.target = m_irradiance.get()});
        pass.SetViewport({.x = 0.0f, .y = static_cast<float>(entry->tile), .width = 9.0f, .height = 1.0f});
        pass.PushFragmentUniforms(0,
                                  Vector4{size, std::log2(size / static_cast<float>(IrradianceSamples)), 0.0f, 0.0f});
        m_irradiancePass->Draw(pass, {&environment, 1});
    }
    entry->captured = true;
    entry->capturedPosition = probe.position;
    entry->capturedVersion = probe.version;
}

bool ProbeRenderer::Upload() {
    std::vector<std::pair<const ReflectionProbeData*, const Entry*>> captured;
    for (const ReflectionProbeData& probe : m_probes) {
        const Entry* entry = FindEntry(probe.id);
        if (entry && entry->captured) {
            captured.emplace_back(&probe, entry);
        }
    }
    // The smallest first, so a probe inside another lights what it holds.
    std::ranges::stable_sort(captured, {}, [](const auto& probe) { return GetVolume(*probe.first); });
    std::vector<ProbeUniforms> uniforms;
    uniforms.reserve(captured.size());
    for (const auto& [probe, entry] : captured) {
        const Vector3 low{std::min(probe->boxMin.x, probe->boxMax.x), std::min(probe->boxMin.y, probe->boxMax.y),
                          std::min(probe->boxMin.z, probe->boxMax.z)};
        const Vector3 high{std::max(probe->boxMin.x, probe->boxMax.x), std::max(probe->boxMin.y, probe->boxMax.y),
                           std::max(probe->boxMin.z, probe->boxMax.z)};
        // Reflections are projected from where the probe was captured, until it is captured where it is now.
        uniforms.push_back({.boxMin = ToVector4(low, std::max(probe->blendDistance, 0.0f)),
                            .boxMax = ToVector4(high, std::max(probe->intensity, 0.0f)),
                            .position = ToVector4(entry->capturedPosition, static_cast<float>(entry->tile))});
    }
    m_count = static_cast<std::uint32_t>(uniforms.size());
    m_state.stats.reflectionProbes = m_count;
    return UploadStorage(*m_state.device, m_buffer, uniforms);
}

Vector4 ProbeRenderer::GetAtlasUniforms() const {
    return {static_cast<float>(ProbeCaptureSize), static_cast<float>(AtlasColumns),
            1.0f / static_cast<float>(AtlasColumns * ProbeCaptureSize),
            1.0f / static_cast<float>(AtlasRows * TileHeight)};
}

void ProbeRenderer::Bind(RenderPass& pass, bool lit) const {
    const bool probes = lit && m_count > 0 && m_atlas && m_irradiance;
    pass.BindFragmentTexture(11, probes ? *m_atlas : *m_noAtlas, *m_state.clampSampler);
    pass.BindFragmentTexture(12, probes ? *m_irradiance : *m_noAtlas, *m_state.clampSampler);
    pass.BindFragmentStorageBuffer(3, m_buffer ? *m_buffer : *m_noProbes);
}

} // namespace Spot::Detail
