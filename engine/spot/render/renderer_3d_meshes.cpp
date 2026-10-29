#include "spot/render/renderer_3d_meshes.h"

#include "spot/core/debug/profiler.h"
#include "spot/render/material.h"
#include "spot/render/mesh.h"

#include "mesh.frag.h"
#include "mesh.vert.h"
#include "mesh_depth_cutout.frag.h"
#include "shadow.frag.h"
#include "shadow.vert.h"
#include "shadow_cutout.frag.h"
#include "skinned_mesh.vert.h"
#include "skinned_shadow.vert.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <unordered_map>

namespace Spot::Detail {

namespace {

struct MaterialUniforms {
    Color baseColor;
    Color emissive;
    float metallic;
    float roughness;
    // 0 without a normal map, so the white texture bound in its place leaves the normals as they are.
    float normalScale;
    float occlusionStrength;
    float doubleSided;
    float worldTextureSize;
    float alphaMode;
    float alphaCutoff;
};

// Whether the two commands draw the same mesh the same way, so they can be instances of one draw.
bool HaveSameLook(const auto& a, const auto& b) {
    return !a.instanceBuffer && !b.instanceBuffer && a.mesh == b.mesh && a.firstIndex == b.firstIndex &&
           a.indexCount == b.indexCount && a.look == b.look && a.mirrored == b.mirrored &&
           (a.skinCount != 0) == (b.skinCount != 0);
}

// Depth needs only the shape and alpha mask; opaque surfaces can share a draw even with different materials.
bool HaveSameDepth(const DrawCommand& a, const DrawCommand& b) {
    if (a.instanceBuffer || b.instanceBuffer || a.mesh != b.mesh || a.firstIndex != b.firstIndex ||
        a.indexCount != b.indexCount || a.mirrored != b.mirrored || a.doubleSided != b.doubleSided ||
        (a.skinCount != 0) != (b.skinCount != 0)) {
        return false;
    }
    const bool cutout = a.alphaMode == AlphaMode::Cutout;
    if (cutout != (b.alphaMode == AlphaMode::Cutout)) {
        return false;
    }
    return !cutout || (a.textures[0] == b.textures[0] && a.color.a == b.color.a && a.alphaCutoff == b.alphaCutoff &&
                       a.nearestFiltering == b.nearestFiltering);
}

// The depth prepass cuts out as the view's pass does, so it reads the alpha at the same, possibly projected, UVs.
bool HaveSamePrepassDepth(const DrawCommand& a, const DrawCommand& b) {
    return HaveSameDepth(a, b) && (a.alphaMode != AlphaMode::Cutout || a.worldTextureSize == b.worldTextureSize);
}

// Back faces culled, front faces for transforms that mirror, none for double-sided materials.
std::size_t GetCullVariant(const DrawCommand& command) {
    return command.doubleSided ? 2 : command.mirrored ? 1 : 0;
}

CullMode GetCullMode(std::size_t variant) {
    return variant % 3 == 0 ? CullMode::Back : variant % 3 == 1 ? CullMode::Front : CullMode::None;
}

} // namespace

MeshBatcher::MeshBatcher(Renderer3DState& state) : m_state(state) {
    GraphicsDevice& device = *state.device;
    const Renderer3DConfig& config = state.config;
    const auto vertexShader = device.CreateShader(Detail::Shaders::MeshVert);
    const auto fragmentShader = device.CreateShader(Detail::Shaders::MeshFrag);
    if (vertexShader && fragmentShader) {
        PipelineDesc desc{
            .vertexShader = vertexShader.get(),
            .fragmentShader = fragmentShader.get(),
            .vertexBuffers = {Mesh::GetVertexLayout()},
            .cullMode = CullMode::Back,
            .targetFormat = config.targetFormat,
            .depth = DepthState{.format = config.depthFormat},
            .sampleCount = config.sampleCount,
        };
        m_pipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::None;
        m_doubleSidedPipeline = device.CreatePipeline(desc);
        // Transparent surfaces are tested against what is opaque, but do not hide what is drawn after them.
        desc.blendMode = BlendMode::Premultiplied;
        desc.depth = DepthState{.format = config.depthFormat, .write = false};
        m_doubleSidedTransparentPipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::Back;
        m_transparentPipeline = device.CreatePipeline(desc);
        desc.cullMode = CullMode::Front;
        m_mirroredTransparentPipeline = device.CreatePipeline(desc);
        desc.blendMode = BlendMode::None;
        desc.depth = DepthState{.format = config.depthFormat};
        m_mirroredPipeline = device.CreatePipeline(desc);
    }

    const auto skinnedVertex = device.CreateShader(Detail::Shaders::SkinnedMeshVert);
    if (skinnedVertex && fragmentShader) {
        for (std::size_t i = 0; i < m_skinnedPipelines.size(); ++i) {
            m_skinnedPipelines[i] =
                device.CreatePipeline({.vertexShader = skinnedVertex.get(),
                                       .fragmentShader = fragmentShader.get(),
                                       .vertexBuffers = {Mesh::GetVertexLayout(), Mesh::GetSkinLayout()},
                                       .blendMode = i >= 3 ? BlendMode::Premultiplied : BlendMode::None,
                                       .cullMode = i % 3 == 0   ? CullMode::Back
                                                   : i % 3 == 1 ? CullMode::Front
                                                                : CullMode::None,
                                       .targetFormat = config.targetFormat,
                                       .depth = DepthState{.format = config.depthFormat, .write = i < 3},
                                       .sampleCount = config.sampleCount});
        }
    }

    // The prepass draws with the view's vertex shaders, so the depths match exactly, and the view's pass then keeps
    // only the fragments at that depth: the nearest, shaded once.
    const auto opaqueDepth = device.CreateShader(Detail::Shaders::ShadowFrag);
    const auto cutoutDepth = device.CreateShader(Detail::Shaders::MeshDepthCutoutFrag);
    if (config.depthPrepass && vertexShader && skinnedVertex && fragmentShader && opaqueDepth && cutoutDepth) {
        for (std::size_t i = 0; i < m_prepassPipelines.size(); ++i) {
            const bool skinned = i >= 6;
            std::vector<VertexBufferLayout> layouts{Mesh::GetVertexLayout()};
            if (skinned) {
                layouts.push_back(Mesh::GetSkinLayout());
            }
            m_prepassPipelines[i] =
                device.CreatePipeline({.vertexShader = skinned ? skinnedVertex.get() : vertexShader.get(),
                                       .fragmentShader = i % 6 >= 3 ? cutoutDepth.get() : opaqueDepth.get(),
                                       .vertexBuffers = layouts,
                                       .cullMode = GetCullMode(i),
                                       .depthOnly = true,
                                       .depth = DepthState{.format = config.depthFormat},
                                       .sampleCount = config.sampleCount});
        }
        for (std::size_t i = 0; i < m_prepassedPipelines.size(); ++i) {
            const bool skinned = i >= 3;
            std::vector<VertexBufferLayout> layouts{Mesh::GetVertexLayout()};
            if (skinned) {
                layouts.push_back(Mesh::GetSkinLayout());
            }
            m_prepassedPipelines[i] = device.CreatePipeline(
                {.vertexShader = skinned ? skinnedVertex.get() : vertexShader.get(),
                 .fragmentShader = fragmentShader.get(),
                 .vertexBuffers = layouts,
                 .cullMode = GetCullMode(i),
                 .targetFormat = config.targetFormat,
                 .depth = DepthState{.format = config.depthFormat, .write = false, .compare = CompareOp::LessOrEqual},
                 .sampleCount = config.sampleCount});
        }
    }
    m_noVisible = device.CreateBuffer(BufferUsage::Storage, std::array<std::uint32_t, 4>{});
}

bool MeshBatcher::IsValid() const {
    return std::ranges::all_of(m_skinnedPipelines, [](const auto& p) { return !!p; }) && m_pipeline &&
           m_doubleSidedPipeline && m_transparentPipeline && m_doubleSidedTransparentPipeline && m_mirroredPipeline &&
           m_mirroredTransparentPipeline && m_noVisible;
}

void MeshBatcher::AddCommand(const RenderMeshObject& object, const Material& material, std::uint32_t level) {
    const RenderMeshLevel& range = object.levels[level];
    m_state.commands.push_back(
        {range.mesh,
         {material.baseColorTexture.Get(), material.metallicRoughnessTexture.Get(), material.normalTexture.Get(),
          material.occlusionTexture.Get(), material.emissiveTexture.Get()},
         material.baseColor,
         material.metallic,
         material.roughness,
         material.normalTexture ? material.normalScale : 0.0f,
         material.occlusionStrength,
         material.emissive,
         material.alphaMode,
         material.alphaCutoff,
         material.doubleSided,
         material.worldTextureSize,
         object.desc.textureSpace,
         object.desc.transform,
         object.bounds,
         object.desc.castsShadows});
    DrawCommand& command = m_state.commands.back();
    command.nearestFiltering = material.nearestFiltering;
    command.firstIndex = range.firstIndex;
    command.indexCount = range.indexCount;
    command.mirrored = object.mirrored;
}

void MeshBatcher::Prepare() {
    // Shadow passes cull against their own views; the view's pass leaves out what the camera cannot see.
    const Frustum view(m_state.viewProjection);
    for (DrawCommand& command : m_state.commands) {
        command.visible = view.Contains(command.bounds.center, command.bounds.radius);
        // A batch the shaders cull counts as seen whole, as far as the CPU can tell.
        (command.visible ? m_state.stats.visibleMeshes : m_state.stats.culledMeshes) += command.instanceCount;
    }
    AssignLooks();
    Profiler::Count("Visible meshes", m_state.stats.visibleMeshes);
    Profiler::Count("Culled meshes", m_state.stats.culledMeshes);

    {
        SPOT_PROFILE_SCOPE("Renderer3D::Sort");
        // Opaque and cutout surfaces are depth tested, so their order only matters for how often state changes.
        // Transparent ones come after them, from the farthest to the nearest, so each blends over what is behind it.
        // Only small keys move while sorting; the commands, which are large, then move once into their places.
        const Vector3 forward = m_state.camera.GetForward();
        const Vector3 eye = m_state.camera.position;
        m_sortEntries.clear();
        m_sortEntries.reserve(m_state.commands.size());
        for (std::size_t i = 0; i < m_state.commands.size(); ++i) {
            DrawCommand& c = m_state.commands[i];
            c.depth = Dot(c.bounds.center - eye, forward);
            const bool transparent = c.alphaMode == AlphaMode::Transparent;
            m_sortEntries.push_back({.transparent = transparent,
                                     .hidden = !c.visible,
                                     .doubleSided = c.doubleSided,
                                     .mirrored = c.mirrored,
                                     .alphaMode = c.alphaMode,
                                     .look = c.look,
                                     .mesh = c.mesh,
                                     .firstIndex = c.firstIndex,
                                     .depth = transparent ? LengthSquared(c.bounds.center - eye) : c.depth,
                                     .index = static_cast<std::uint32_t>(i)});
        }
        // Opaque commands in view come first, so those out of it do not split runs of instances. Then they go by
        // pipeline, then material, so state changes as rarely as it can, then mesh, so those that look the same are
        // side by side to be drawn as instances of one draw, and last from the nearest to the farthest, so what is in
        // front hides what is behind before it is shaded.
        std::stable_sort(m_sortEntries.begin(), m_sortEntries.end(), [](const SortEntry& a, const SortEntry& b) {
            if (a.transparent != b.transparent) {
                return !a.transparent;
            }
            if (a.transparent) {
                return a.depth > b.depth;
            }
            if (a.hidden != b.hidden) {
                return !a.hidden;
            }
            if (a.doubleSided != b.doubleSided) {
                return !a.doubleSided;
            }
            if (a.mirrored != b.mirrored) {
                return !a.mirrored;
            }
            if (a.alphaMode != b.alphaMode) {
                return a.alphaMode < b.alphaMode;
            }
            if (a.look != b.look) {
                return a.look < b.look;
            }
            if (a.mesh != b.mesh) {
                return std::less<const Mesh*>()(a.mesh, b.mesh);
            }
            if (a.firstIndex != b.firstIndex) {
                return a.firstIndex < b.firstIndex;
            }
            return a.depth < b.depth;
        });
        m_sortedCommands.clear();
        m_sortedCommands.reserve(m_state.commands.size());
        for (const SortEntry& entry : m_sortEntries) {
            m_sortedCommands.push_back(std::move(m_state.commands[entry.index]));
        }
        m_state.commands.swap(m_sortedCommands);
    }
}

void MeshBatcher::AssignLooks() {
    // Materials look the same when their textures and every value match, as copies of a material do.
    struct Look {
        MaterialTextures textures;
        std::array<float, 16> values;
        bool operator==(const Look&) const = default;
    };
    struct LookHash {
        std::size_t operator()(const Look& look) const {
            std::size_t hash = 0;
            const auto mix = [&hash](std::size_t value) {
                hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
            };
            for (const Texture* texture : look.textures) {
                mix(std::hash<const Texture*>()(texture));
            }
            for (const float value : look.values) {
                mix(std::hash<float>()(value));
            }
            return hash;
        }
    };
    std::unordered_map<Look, std::uint32_t, LookHash> looks;
    for (DrawCommand& c : m_state.commands) {
        const Look look{c.textures,
                        {c.color.r, c.color.g, c.color.b, c.color.a, c.metallic, c.roughness, c.normalScale,
                         c.occlusionStrength, c.emissive.r, c.emissive.g, c.emissive.b, c.alphaCutoff,
                         c.worldTextureSize, static_cast<float>(c.alphaMode), c.doubleSided ? 1.0f : 0.0f,
                         c.nearestFiltering ? 1.0f : 0.0f}};
        c.look = looks.try_emplace(look, static_cast<std::uint32_t>(looks.size())).first->second;
    }
}

bool MeshBatcher::UploadInstances() {
    SPOT_PROFILE_SCOPE("Renderer3D::UploadInstances");
    if (m_state.commands.empty()) {
        return true;
    }
    std::vector<InstanceData> instances;
    instances.reserve(m_state.commands.size());
    for (const DrawCommand& command : m_state.commands) {
        instances.push_back({command.transform,
                             GetNormalMatrix(command.transform, command.textureSpace),
                             {command.skinOffset, command.skinCount, 0, 0},
                             ToVector4(command.bounds.center, command.bounds.radius)});
    }
    return UploadStorage(*m_state.device, m_state.instances, instances) &&
           UploadStorage(*m_state.device, m_state.palettes, m_state.paletteData);
}

const Pipeline* MeshBatcher::GetPipeline(const DrawCommand& command, bool depthPrepassed) const {
    if (depthPrepassed && command.alphaMode != AlphaMode::Transparent) {
        return m_prepassedPipelines[GetCullVariant(command) + (command.skinCount ? 3 : 0)].get();
    }
    if (command.skinCount) {
        const auto variant = (command.alphaMode == AlphaMode::Transparent ? 3u : 0u) + (command.doubleSided ? 2u
                                                                                        : command.mirrored  ? 1u
                                                                                                            : 0u);
        return m_skinnedPipelines[variant].get();
    }
    if (command.alphaMode == AlphaMode::Transparent) {
        return command.doubleSided ? m_doubleSidedTransparentPipeline.get()
               : command.mirrored  ? m_mirroredTransparentPipeline.get()
                                   : m_transparentPipeline.get();
    }
    return command.doubleSided ? m_doubleSidedPipeline.get()
           : command.mirrored  ? m_mirroredPipeline.get()
                               : m_pipeline.get();
}

void MeshBatcher::Flush(RenderPass& pass, bool depthPrepassed, MeshLayer layer) {
    SPOT_PROFILE_SCOPE("Renderer3D::Flush");
    const auto inLayer = [layer](const DrawCommand& command) {
        return command.visible && (command.alphaMode == AlphaMode::Transparent) == (layer == MeshLayer::Transparent);
    };
    if (std::ranges::none_of(m_state.commands, inLayer)) {
        return;
    }

    // Surfaces drawn before may have changed what the view's uniforms and the instances' slots hold.
    pass.PushVertexUniforms(0, MakeFrameUniforms(m_state.viewProjection, m_state.camera));
    pass.BindVertexStorageBuffer(0, *m_state.instances);
    const Buffer* boundInstances = m_state.instances.get();
    const Buffer* boundVisible = nullptr;
    const Pipeline* boundPipeline = nullptr;
    MaterialTextures boundTextures{};
    const Sampler* boundSampler = nullptr;
    const Mesh* boundMesh = nullptr;
    std::optional<MaterialUniforms> boundMaterial;
    for (std::size_t first = 0; first < m_state.commands.size();) {
        const DrawCommand& command = m_state.commands[first];
        if (!inLayer(command)) {
            ++first;
            continue;
        }
        // Batches are runs of commands that look the same, which sorting gathers, all in view.
        std::size_t end = first + 1;
        while (end < m_state.commands.size() && inLayer(m_state.commands[end]) &&
               HaveSameLook(command, m_state.commands[end])) {
            ++end;
        }
        const auto instanceCount = static_cast<std::uint32_t>(end - first);
        const Pipeline* pipeline = GetPipeline(command, depthPrepassed);
        if (pipeline != boundPipeline) {
            pass.BindPipeline(*pipeline);
            boundPipeline = pipeline;
            ++m_state.stats.pipelineChanges;
        }
        if (command.skinCount) {
            pass.BindVertexStorageBuffer(1, *m_state.palettes);
            pass.BindVertexBuffer(1, *command.mesh->GetSkinBuffer());
            boundVisible = nullptr;
        }
        // Missing textures are white, which leaves the values they multiply as they are.
        bool materialChanged = false;
        for (std::size_t i = 0; i < command.textures.size(); ++i) {
            const Texture* texture = command.textures[i] ? command.textures[i] : m_state.whiteTexture.get();
            const Sampler* sampler = command.nearestFiltering ? m_state.nearestSampler.get() : m_state.sampler.get();
            if (texture != boundTextures[i] || sampler != boundSampler) {
                pass.BindFragmentTexture(i == 0 ? 0 : static_cast<std::uint32_t>(i + 2), *texture, *sampler);
                boundTextures[i] = texture;
                materialChanged = true;
            }
        }
        boundSampler = command.nearestFiltering ? m_state.nearestSampler.get() : m_state.sampler.get();
        const MaterialUniforms material{.baseColor = command.color,
                                        .emissive = command.emissive,
                                        .metallic = command.metallic,
                                        .roughness = command.roughness,
                                        .normalScale = command.normalScale,
                                        .occlusionStrength = command.occlusionStrength,
                                        .doubleSided = command.doubleSided ? 1.0f : 0.0f,
                                        .worldTextureSize = command.worldTextureSize,
                                        .alphaMode = static_cast<float>(command.alphaMode),
                                        .alphaCutoff = command.alphaCutoff};
        if (!boundMaterial || std::memcmp(&*boundMaterial, &material, sizeof(MaterialUniforms)) != 0) {
            pass.PushFragmentUniforms(1, material);
            boundMaterial = material;
            materialChanged = true;
        }
        m_state.stats.materialChanges += materialChanged ? 1 : 0;
        if (command.mesh != boundMesh) {
            if (command.skinCount) {
                pass.BindVertexStorageBuffer(1, *m_state.palettes);
                pass.BindVertexBuffer(1, *command.mesh->GetSkinBuffer());
            }
            pass.BindVertexBuffer(0, command.mesh->GetVertexBuffer());
            pass.BindIndexBuffer(command.mesh->GetIndexBuffer(), IndexType::UInt32);
            boundMesh = command.mesh;
        }
        const Buffer* instanceBuffer = command.instanceBuffer ? command.instanceBuffer : m_state.instances.get();
        if (instanceBuffer != boundInstances) {
            pass.BindVertexStorageBuffer(0, *instanceBuffer);
            boundInstances = instanceBuffer;
        }
        const Buffer* visible = command.keptInstances ? command.keptInstances : m_noVisible.get();
        // Skinned meshes read their palettes there instead.
        if (!command.skinCount && visible != boundVisible) {
            pass.BindVertexStorageBuffer(1, *visible);
            boundVisible = visible;
        }
        if (command.arguments) {
            pass.PushVertexUniforms(1, BatchUniforms{{command.instanceFirst, 2, 0, 0}});
            pass.DrawIndexedIndirect(*command.arguments,
                                     command.batch * static_cast<std::uint32_t>(sizeof(DrawIndexedIndirectArguments)));
        } else if (command.instanceBuffer) {
            pass.PushVertexUniforms(1, BatchUniforms{{command.instanceFirst, 1, 0, 0}});
            pass.DrawIndexed(command.indexCount, command.instanceCount, command.firstIndex);
        } else {
            pass.PushVertexUniforms(1, BatchUniforms{{static_cast<std::uint32_t>(first), 0, 0, 0}});
            pass.DrawIndexed(command.indexCount, instanceCount, command.firstIndex);
        }

        ++m_state.stats.drawCalls;
        m_state.stats.triangleCount +=
            command.indexCount / 3 * (command.instanceBuffer ? command.instanceCount : instanceCount);
        first = end;
    }
}

bool MeshBatcher::DrawDepthPrepass(RenderContext& context) {
    Texture* depth = m_state.target.depthTarget;
    const auto made = [](const auto& pipeline) { return pipeline != nullptr; };
    if (!depth || !std::ranges::all_of(m_prepassPipelines, made) || !std::ranges::all_of(m_prepassedPipelines, made)) {
        return false;
    }
    SPOT_PROFILE_SCOPE("Renderer3D::DrawDepthPrepass");
    m_prepassOrder.clear();
    for (std::size_t i = 0; i < m_state.commands.size(); ++i) {
        if (m_state.commands[i].visible && m_state.commands[i].alphaMode != AlphaMode::Transparent) {
            m_prepassOrder.push_back(static_cast<std::uint32_t>(i));
        }
    }
    if (m_prepassOrder.empty()) {
        return false;
    }
    const auto bytes = static_cast<std::uint32_t>(m_prepassOrder.size() * sizeof(std::uint32_t));
    if (!m_prepassIndices || m_prepassIndices->GetSize() < bytes) {
        m_prepassIndices = m_state.device->CreateBuffer({.usage = BufferUsage::Storage, .size = bytes});
        if (!m_prepassIndices) {
            return false;
        }
    }
    m_state.device->UploadToBuffer(*m_prepassIndices, m_prepassOrder, 0, UploadMode::Discard);

    auto pass =
        context.BeginRenderPass({.depthTarget = depth, .clearDepth = m_state.target.clearDepth, .depthOnly = true});
    // As the view's pass pushes them, since batches of render worlds cull their instances against the view's planes.
    pass.PushVertexUniforms(0, MakeFrameUniforms(m_state.viewProjection, m_state.camera));
    // Slot 1 holds the order, which direct draws read their commands through so repeated meshes share a draw, the
    // instances a culling pass kept, or the joints of skinned meshes.
    const Buffer* boundInstances = nullptr;
    const Buffer* boundSecond = nullptr;
    const auto bind = [&pass](const Buffer*& bound, std::uint32_t slot, const Buffer& buffer) {
        if (bound != &buffer) {
            pass.BindVertexStorageBuffer(slot, buffer);
            bound = &buffer;
        }
    };
    const Pipeline* boundPipeline = nullptr;
    const Mesh* boundMesh = nullptr;
    const Texture* boundTexture = nullptr;
    const Sampler* boundSampler = nullptr;
    std::optional<Vector4> boundCutout;
    std::uint32_t draws = 0;
    for (std::size_t i = 0; i < m_prepassOrder.size();) {
        const DrawCommand& command = m_state.commands[m_prepassOrder[i]];
        std::size_t end = i + 1;
        // Skinned meshes read their instances in the commands' order, so only neighbors share a draw.
        while (end < m_prepassOrder.size() && HaveSamePrepassDepth(command, m_state.commands[m_prepassOrder[end]]) &&
               (!command.skinCount || m_prepassOrder[end] == m_prepassOrder[end - 1] + 1)) {
            ++end;
        }
        const bool cutout = command.alphaMode == AlphaMode::Cutout;
        const Pipeline* pipeline =
            m_prepassPipelines[GetCullVariant(command) + (cutout ? 3 : 0) + (command.skinCount ? 6 : 0)].get();
        if (pipeline != boundPipeline) {
            pass.BindPipeline(*pipeline);
            boundPipeline = pipeline;
        }
        if (cutout) {
            const Texture* texture = command.textures[0] ? command.textures[0] : m_state.whiteTexture.get();
            const Sampler* sampler = command.nearestFiltering ? m_state.nearestSampler.get() : m_state.sampler.get();
            if (texture != boundTexture || sampler != boundSampler) {
                pass.BindFragmentTexture(0, *texture, *sampler);
                boundTexture = texture;
                boundSampler = sampler;
            }
            const Vector4 values{command.color.a, command.alphaCutoff, command.worldTextureSize, 0.0f};
            if (!boundCutout || *boundCutout != values) {
                pass.PushFragmentUniforms(0, values);
                boundCutout = values;
            }
        }
        if (command.mesh != boundMesh) {
            pass.BindVertexBuffer(0, command.mesh->GetVertexBuffer());
            pass.BindIndexBuffer(command.mesh->GetIndexBuffer(), IndexType::UInt32);
            boundMesh = command.mesh;
        }
        if (command.skinCount) {
            pass.BindVertexBuffer(1, *command.mesh->GetSkinBuffer());
        }
        if (command.instanceBuffer) {
            bind(boundInstances, 0, *command.instanceBuffer);
            if (command.arguments) {
                bind(boundSecond, 1, *command.keptInstances);
                pass.PushVertexUniforms(1, BatchUniforms{{command.instanceFirst, 2, 0, 0}});
                pass.DrawIndexedIndirect(*command.arguments, command.batch * static_cast<std::uint32_t>(
                                                                                 sizeof(DrawIndexedIndirectArguments)));
            } else {
                bind(boundSecond, 1, *m_prepassIndices);
                pass.PushVertexUniforms(1, BatchUniforms{{command.instanceFirst, 1, 0, 0}});
                pass.DrawIndexed(command.indexCount, command.instanceCount, command.firstIndex);
            }
        } else {
            bind(boundInstances, 0, *m_state.instances);
            if (command.skinCount) {
                bind(boundSecond, 1, *m_state.palettes);
                pass.PushVertexUniforms(1, BatchUniforms{{m_prepassOrder[i], 0, 0, 0}});
            } else {
                bind(boundSecond, 1, *m_prepassIndices);
                pass.PushVertexUniforms(1, BatchUniforms{{static_cast<std::uint32_t>(i), 2, 0, 0}});
            }
            pass.DrawIndexed(command.indexCount, static_cast<std::uint32_t>(end - i), command.firstIndex);
        }
        ++draws;
        i = end;
    }
    Profiler::Count("Depth prepass draw calls", draws);
    return true;
}

const Texture* MeshBatcher::RenderCameraDepth(RenderContext& context) {
    SPOT_PROFILE_SCOPE("Renderer3D::RenderCameraDepth");
    const auto width = static_cast<std::uint32_t>(m_state.viewSize.x),
               height = static_cast<std::uint32_t>(m_state.viewSize.y);
    if (!m_cameraDepth || m_cameraDepth->GetWidth() != width || m_cameraDepth->GetHeight() != height) {
        m_cameraDepth = m_state.device->CreateTexture(
            {.width = width, .height = height, .format = TextureFormat::Depth32Float, .renderTarget = true});
    }
    if (!m_cameraDepth)
        return nullptr;
    if (!m_cameraDepthPipelines[0]) {
        const auto vertex = m_state.device->CreateShader(Shaders::ShadowVert);
        const auto skinned = m_state.device->CreateShader(Shaders::SkinnedShadowVert);
        const auto opaque = m_state.device->CreateShader(Shaders::ShadowFrag);
        const auto cutout = m_state.device->CreateShader(Shaders::ShadowCutoutFrag);
        if (!vertex || !skinned || !opaque || !cutout)
            return nullptr;
        for (std::size_t i = 0; i < m_cameraDepthPipelines.size(); ++i) {
            std::vector<VertexBufferLayout> layouts{
                {.stride = sizeof(MeshVertex),
                 .attributes = {{0, VertexFormat::Float3, offsetof(MeshVertex, position)},
                                {1, VertexFormat::Float2, offsetof(MeshVertex, uv)}}}};
            if (i >= 6) {
                layouts.push_back(Mesh::GetSkinLayout(2));
            }
            m_cameraDepthPipelines[i] =
                m_state.device->CreatePipeline({.vertexShader = i >= 6 ? skinned.get() : vertex.get(),
                                                .fragmentShader = i % 6 >= 3 ? cutout.get() : opaque.get(),
                                                .vertexBuffers = layouts,
                                                .cullMode = i % 3 == 0   ? CullMode::Back
                                                            : i % 3 == 1 ? CullMode::Front
                                                                         : CullMode::None,
                                                .depthOnly = true,
                                                .depth = DepthState{}});
        }
    }
    for (const auto& pipeline : m_cameraDepthPipelines)
        if (!pipeline)
            return nullptr;
    m_cameraDepthOrder.clear();
    for (std::size_t i = 0; i < m_state.commands.size(); ++i) {
        if (m_state.commands[i].visible && m_state.commands[i].alphaMode != AlphaMode::Transparent)
            m_cameraDepthOrder.push_back(static_cast<std::uint32_t>(i));
    }
    if (!m_cameraDepthOrder.empty()) {
        const auto bytes = static_cast<std::uint32_t>(m_cameraDepthOrder.size() * sizeof(std::uint32_t));
        if (!m_cameraDepthIndices || m_cameraDepthIndices->GetSize() < bytes)
            m_cameraDepthIndices = m_state.device->CreateBuffer({.usage = BufferUsage::Storage, .size = bytes});
        if (!m_cameraDepthIndices)
            return nullptr;
        m_state.device->UploadToBuffer(*m_cameraDepthIndices, m_cameraDepthOrder, 0, UploadMode::Discard);
    }
    const std::uint32_t surfaceView =
        m_state.PrepareSurfaces(context, SurfacePass::Depth, m_state.viewProjection, false);
    auto pass = context.BeginRenderPass({.depthTarget = m_cameraDepth.get(), .clearDepth = 1.0f, .depthOnly = true});
    if (!m_cameraDepthOrder.empty()) {
        pass.BindVertexStorageBuffer(0, *m_state.instances);
        pass.BindVertexStorageBuffer(1, *m_cameraDepthIndices);
        // The view, and its planes, which batches of render worlds are culled against in the shader.
        struct {
            Matrix4 viewProjection;
            std::array<Vector4, 6> cullPlanes;
            Vector4 lodView;
        } view{m_state.viewProjection, Frustum(m_state.viewProjection).GetPlanes(), GetLodView(m_state.camera)};
        pass.PushVertexUniforms(0, view);
        const Pipeline* boundPipeline = nullptr;
        const Mesh* boundMesh = nullptr;
        const Texture* boundTexture = nullptr;
        const Sampler* boundSampler = nullptr;
        std::optional<Vector4> boundCutout;
        std::uint32_t draws = 0;
        for (std::size_t i = 0; i < m_cameraDepthOrder.size();) {
            const auto& command = m_state.commands[m_cameraDepthOrder[i]];
            std::size_t end = i + 1;
            while (end < m_cameraDepthOrder.size() &&
                   HaveSameDepth(command, m_state.commands[m_cameraDepthOrder[end]])) {
                ++end;
            }
            const bool cutout = command.alphaMode == AlphaMode::Cutout;
            const auto variant = static_cast<std::size_t>((cutout ? 3 : 0) + (command.doubleSided ? 2
                                                                              : command.mirrored  ? 1
                                                                                                  : 0));
            const Pipeline* pipeline = m_cameraDepthPipelines[variant + (command.skinCount ? 6 : 0)].get();
            if (pipeline != boundPipeline) {
                pass.BindPipeline(*pipeline);
                boundPipeline = pipeline;
            }
            if (command.skinCount) {
                pass.BindVertexStorageBuffer(2, *m_state.palettes);
                pass.BindVertexBuffer(1, *command.mesh->GetSkinBuffer());
            }
            if (cutout) {
                const Texture* texture = command.textures[0] ? command.textures[0] : m_state.whiteTexture.get();
                const Sampler* sampler =
                    command.nearestFiltering ? m_state.nearestSampler.get() : m_state.sampler.get();
                if (texture != boundTexture || sampler != boundSampler) {
                    pass.BindFragmentTexture(0, *texture, *sampler);
                    boundTexture = texture;
                    boundSampler = sampler;
                }
                const Vector4 alpha{command.color.a, command.alphaCutoff, 0, 0};
                if (!boundCutout || *boundCutout != alpha) {
                    pass.PushFragmentUniforms(0, alpha);
                    boundCutout = alpha;
                }
            }
            if (command.mesh != boundMesh) {
                pass.BindVertexBuffer(0, command.mesh->GetVertexBuffer());
                pass.BindIndexBuffer(command.mesh->GetIndexBuffer(), IndexType::UInt32);
                boundMesh = command.mesh;
            }
            if (command.instanceBuffer) {
                pass.BindVertexStorageBuffer(0, *command.instanceBuffer);
                if (command.arguments) {
                    pass.BindVertexStorageBuffer(1, *command.keptInstances);
                    pass.PushVertexUniforms(1, BatchUniforms{{command.instanceFirst, 2, 0, 0}});
                    pass.DrawIndexedIndirect(*command.arguments, command.batch * static_cast<std::uint32_t>(sizeof(
                                                                                     DrawIndexedIndirectArguments)));
                    pass.BindVertexStorageBuffer(1, *m_cameraDepthIndices);
                } else {
                    pass.PushVertexUniforms(1, BatchUniforms{{command.instanceFirst, 1, 0, 0}});
                    pass.DrawIndexed(command.indexCount, command.instanceCount, command.firstIndex);
                }
                pass.BindVertexStorageBuffer(0, *m_state.instances);
            } else {
                pass.PushVertexUniforms(1, std::array<std::uint32_t, 4>{static_cast<std::uint32_t>(i), 0, 0, 0});
                pass.DrawIndexed(command.indexCount, static_cast<std::uint32_t>(end - i), command.firstIndex);
            }
            ++draws;
            i = end;
        }
        Profiler::Count("Camera depth draw calls", draws);
    }
    m_state.DrawSurfaces(pass, SurfacePass::Depth, m_state.viewProjection, false, surfaceView);
    return m_cameraDepth.get();
}

} // namespace Spot::Detail
