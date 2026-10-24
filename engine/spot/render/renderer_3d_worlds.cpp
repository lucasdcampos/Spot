#include "spot/render/renderer_3d_worlds.h"

#include "spot/core/debug/profiler.h"
#include "spot/graphics/compute.h"

#include "hiz_reduce.frag.h"
#include "world_cull.comp.h"

#include <algorithm>
#include <bit>
#include <span>
#include <tuple>

namespace Spot::Detail {

namespace {

// The side, in texels, of the tiles of depth that occlusion culling tests against.
constexpr std::uint32_t OcclusionTileSize = 16;

// A sphere around the spheres.
BoundingSphere Enclose(std::span<const BoundingSphere> spheres) {
    if (spheres.empty()) {
        return {};
    }
    Vector3 low = spheres[0].center;
    Vector3 high = low;
    for (const BoundingSphere& sphere : spheres) {
        const Vector3 r{sphere.radius, sphere.radius, sphere.radius};
        low = {std::min(low.x, sphere.center.x - r.x), std::min(low.y, sphere.center.y - r.y),
               std::min(low.z, sphere.center.z - r.z)};
        high = {std::max(high.x, sphere.center.x + r.x), std::max(high.y, sphere.center.y + r.y),
                std::max(high.z, sphere.center.z + r.z)};
    }
    const Vector3 center = (low + high) * 0.5f;
    float radius = 0.0f;
    for (const BoundingSphere& sphere : spheres) {
        radius = std::max(radius, Length(sphere.center - center) + sphere.radius);
    }
    return {center, radius};
}

} // namespace

WorldCuller::WorldCuller(Renderer3DState& state) : m_state(state) {
    // Occlusion culling needs compute shaders, indirect draws and depth tiles it can draw; without them the worlds'
    // instances are culled in the vertex shader against the view alone, and every one in view shows.
    GraphicsDevice& device = *state.device;
    if (!device.GetCapabilities().compute || !device.GetCapabilities().indirectDraws ||
        !device.SupportsRenderTarget(TextureFormat::R32Float)) {
        return;
    }
    m_tilePass = FullscreenPass::Create(device, Shaders::HizReduceFrag, {.targetFormat = TextureFormat::R32Float});
    if (const auto shader = device.CreateShader(Shaders::WorldCullComp)) {
        m_pipeline = device.CreateComputePipeline({.shader = shader.get()});
    }
    m_sampler = device.CreateSampler({.filter = TextureFilter::Nearest});
    m_noTiles = device.CreateTexture({.width = 1, .height = 1, .format = TextureFormat::R32Float});
    if (m_noTiles) {
        device.UploadToTexture(*m_noTiles, std::array<float, 1>{1.0f});
    }
}

WorldCache* WorldCuller::Update(const RenderWorld& world) {
    SPOT_PROFILE_SCOPE("UpdateWorldCache");
    auto found = std::ranges::find_if(m_caches, [&](const auto& cache) { return cache->worldId == world.GetId(); });
    if (found == m_caches.end()) {
        m_caches.push_back(std::make_unique<WorldCache>());
        m_caches.back()->worldId = world.GetId();
        found = m_caches.end() - 1;
    }
    WorldCache& cache = **found;
    cache.lastDrawn = m_state.endCount;
    const std::span<const RenderMeshObject> objects = world.GetMeshes();
    // The range of screen sizes an instance shows at goes where skinned instances keep their skin, which world
    // instances have none of.
    const auto makeInstance = [](const RenderMeshObject& object, std::uint32_t level) {
        const RenderMeshLevel& range = object.levels[level];
        return InstanceData{object.desc.transform,
                            GetNormalMatrix(object.desc.transform),
                            {0, 0, std::bit_cast<std::uint32_t>(range.minScreenSize),
                             std::bit_cast<std::uint32_t>(range.maxScreenSize)},
                            ToVector4(object.bounds.center, object.bounds.radius)};
    };

    if (!cache.built || cache.structureVersion != world.GetStructureVersion()) {
        // Each level of detail of each object is an instance, and instances drawn the same way go side by side, as
        // one batch.
        struct Entry {
            std::uint32_t object;
            std::uint32_t level;
        };
        std::vector<Entry> entries;
        for (std::uint32_t i = 0; i < objects.size(); ++i) {
            if (objects[i].desc.mesh) {
                for (std::uint32_t level = 0; level < objects[i].levelCount; ++level) {
                    if (objects[i].levels[level].indexCount > 0) {
                        entries.push_back({i, level});
                    }
                }
            }
        }
        const auto key = [&](const Entry& entry) {
            const RenderMeshObject& o = objects[entry.object];
            const RenderMeshLevel& range = o.levels[entry.level];
            return std::make_tuple(o.desc.layer, reinterpret_cast<std::uintptr_t>(o.desc.material),
                                   reinterpret_cast<std::uintptr_t>(range.mesh), range.firstIndex, range.indexCount,
                                   o.mirrored, o.desc.castsShadows);
        };
        // Within those, near ones go together, so each batch covers a small part of the world that views can leave
        // out whole: they are ordered along a Morton curve through the world's bounds, and cut every WorldBatchSize.
        Vector3 low{};
        Vector3 high{};
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const Vector3 c = objects[entries[i].object].bounds.center;
            low = i == 0 ? c : Vector3{std::min(low.x, c.x), std::min(low.y, c.y), std::min(low.z, c.z)};
            high = i == 0 ? c : Vector3{std::max(high.x, c.x), std::max(high.y, c.y), std::max(high.z, c.z)};
        }
        const auto spread = [](std::uint32_t v) {
            std::uint32_t x = v & 0x3FFu;
            x = (x | (x << 16)) & 0x030000FFu;
            x = (x | (x << 8)) & 0x0300F00Fu;
            x = (x | (x << 4)) & 0x030C30C3u;
            x = (x | (x << 2)) & 0x09249249u;
            return x;
        };
        const auto cell = [](float value, float from, float to) {
            const float t = to > from ? (value - from) / (to - from) : 0.0f;
            return static_cast<std::uint32_t>(std::clamp(t, 0.0f, 1.0f) * 1023.0f);
        };
        std::vector<std::uint32_t> codes(objects.size());
        for (const Entry& entry : entries) {
            const Vector3 c = objects[entry.object].bounds.center;
            codes[entry.object] = spread(cell(c.x, low.x, high.x)) | (spread(cell(c.y, low.y, high.y)) << 1) |
                                  (spread(cell(c.z, low.z, high.z)) << 2);
        }
        std::ranges::sort(entries, [&](const Entry& a, const Entry& b) {
            const auto ka = key(a);
            const auto kb = key(b);
            return ka != kb ? ka < kb : codes[a.object] < codes[b.object];
        });

        cache.objects.resize(entries.size());
        cache.levels.resize(entries.size());
        cache.batchOfInstance.resize(entries.size());
        cache.instanceData.clear();
        cache.instanceData.reserve(entries.size());
        cache.batches.clear();
        for (std::uint32_t i = 0; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            const RenderMeshObject& object = objects[entry.object];
            const RenderMeshLevel& range = object.levels[entry.level];
            cache.objects[i] = entry.object;
            cache.levels[i] = entry.level;
            cache.instanceData.push_back(makeInstance(object, entry.level));
            if (cache.batches.empty() || cache.batches.back().count == WorldBatchSize ||
                key(entries[cache.batches.back().first]) != key(entry)) {
                cache.batches.push_back({.mesh = range.mesh,
                                         .material = object.desc.material,
                                         .firstIndex = range.firstIndex,
                                         .indexCount = range.indexCount,
                                         .mirrored = object.mirrored,
                                         .castsShadows = object.desc.castsShadows,
                                         .layer = object.desc.layer,
                                         .first = i,
                                         .count = 0,
                                         .bounds = {},
                                         .boundsChanged = true});
            }
            ++cache.batches.back().count;
            cache.batchOfInstance[i] = static_cast<std::uint32_t>(cache.batches.size() - 1);
        }
        // The instances of each object, as offsets into the list of them.
        cache.slotStarts.assign(objects.size() + 1, 0);
        for (const Entry& entry : entries) {
            ++cache.slotStarts[entry.object + 1];
        }
        for (std::size_t i = 1; i < cache.slotStarts.size(); ++i) {
            cache.slotStarts[i] += cache.slotStarts[i - 1];
        }
        cache.slots.resize(entries.size());
        std::vector<std::uint32_t> filled(cache.slotStarts.begin(), cache.slotStarts.end() - 1);
        for (std::uint32_t i = 0; i < entries.size(); ++i) {
            cache.slots[filled[entries[i].object]++] = i;
        }

        const auto size =
            static_cast<std::uint32_t>(std::max<std::size_t>(cache.instanceData.size(), 1) * sizeof(InstanceData));
        if (!cache.instances || cache.instances->GetSize() < size) {
            std::uint32_t capacity = 64 * sizeof(InstanceData);
            while (capacity < size) {
                capacity *= 2;
            }
            cache.instances = m_state.device->CreateBuffer({.usage = BufferUsage::Storage, .size = capacity});
        }
        if (!cache.instances) {
            cache.built = false;
            return nullptr;
        }
        if (!cache.instanceData.empty()) {
            m_state.device->UploadToBuffer(*cache.instances, std::span<const InstanceData>(cache.instanceData), 0,
                                           UploadMode::Discard);
        }
        cache.built = true;
        cache.structureVersion = world.GetStructureVersion();
        cache.batchTable.reset();
    } else if (cache.changeCount != world.GetChangeCount()) {
        // Only the instances that moved go up again, in runs of neighbors.
        const std::span<const std::uint64_t> stamps = world.GetChangeStamps();
        std::vector<std::uint32_t> changed;
        for (std::uint32_t i = 0; i < stamps.size(); ++i) {
            if (stamps[i] <= cache.changeCount) {
                continue;
            }
            for (std::uint32_t s = cache.slotStarts[i]; s < cache.slotStarts[i + 1]; ++s) {
                const std::uint32_t slot = cache.slots[s];
                cache.instanceData[slot] = makeInstance(objects[i], cache.levels[slot]);
                cache.batches[cache.batchOfInstance[slot]].boundsChanged = true;
                changed.push_back(slot);
            }
        }
        std::ranges::sort(changed);
        constexpr std::size_t MaxRuns = 64;
        std::size_t runs = 0;
        for (std::size_t i = 0; i < changed.size() && runs <= MaxRuns; ++i) {
            runs += i == 0 || changed[i] != changed[i - 1] + 1 ? 1 : 0;
        }
        if (runs > MaxRuns) {
            m_state.device->UploadToBuffer(*cache.instances, std::span<const InstanceData>(cache.instanceData), 0,
                                           UploadMode::Preserve);
        } else {
            for (std::size_t first = 0; first < changed.size();) {
                std::size_t end = first + 1;
                while (end < changed.size() && changed[end] == changed[end - 1] + 1) {
                    ++end;
                }
                m_state.device->UploadToBuffer(
                    *cache.instances,
                    std::span<const InstanceData>(cache.instanceData.data() + changed[first], end - first),
                    static_cast<std::uint32_t>(changed[first] * sizeof(InstanceData)), UploadMode::Preserve);
                first = end;
            }
        }
    }
    cache.changeCount = world.GetChangeCount();

    std::vector<BoundingSphere> spheres;
    for (WorldBatch& batch : cache.batches) {
        if (!batch.boundsChanged) {
            continue;
        }
        spheres.clear();
        for (std::uint32_t i = batch.first; i < batch.first + batch.count; ++i) {
            spheres.push_back(objects[cache.objects[i]].bounds);
        }
        batch.bounds = Enclose(spheres);
        batch.boundsChanged = false;
    }
    return &cache;
}

void WorldCuller::Cull() {
    if (!m_pipeline || !m_noTiles) {
        return;
    }
    SPOT_PROFILE_SCOPE("CullWorlds");
    // Only a frame of the same view culls against the last one's depth, which another view's would not hide.
    const Texture* depth = m_state.target.depthTarget;
    const bool occlusion = m_valid && m_depth == depth && m_tiles &&
                           m_tiles->GetWidth() == (depth->GetWidth() + OcclusionTileSize - 1) / OcclusionTileSize &&
                           m_tiles->GetHeight() == (depth->GetHeight() + OcclusionTileSize - 1) / OcclusionTileSize;
    struct Uniforms {
        std::array<Vector4, 6> cullPlanes;
        Vector4 lodView;
        Matrix4 occlusionViewProjection;
        std::array<std::uint32_t, 4> occlusion;
    };
    const Uniforms uniforms{Frustum(m_state.viewProjection).GetPlanes(),
                            GetLodView(m_state.camera),
                            m_viewProjection,
                            {occlusion ? 1u : 0u, OcclusionTileSize, occlusion ? m_depth->GetWidth() : 1u,
                             occlusion ? m_depth->GetHeight() : 1u}};
    for (const auto& cache : m_caches) {
        if (cache->lastDrawn != m_state.endCount || cache->batches.empty()) {
            continue;
        }
        const auto batches = static_cast<std::uint32_t>(cache->batches.size());
        const auto instances = static_cast<std::uint32_t>(cache->instanceData.size());
        const auto ensure = [this](std::unique_ptr<Buffer>& buffer, BufferUsage usage, std::uint32_t size) {
            if (!buffer || buffer->GetSize() < size) {
                buffer = m_state.device->CreateBuffer({.usage = usage, .size = size});
            }
            return buffer != nullptr;
        };
        if (!cache->batchTable || cache->batchTable->GetSize() < batches * sizeof(std::array<std::uint32_t, 4>)) {
            std::vector<std::array<std::uint32_t, 4>> table;
            table.reserve(batches);
            for (const WorldBatch& batch : cache->batches) {
                table.push_back({batch.first, batch.count, batch.indexCount, batch.firstIndex});
            }
            cache->batchTable = m_state.device->CreateBuffer(BufferUsage::Storage, table);
        }
        if (!cache->batchTable || !ensure(cache->visible, BufferUsage::Storage, instances * 4) ||
            !ensure(cache->arguments, BufferUsage::Indirect, batches * sizeof(DrawIndexedIndirectArguments))) {
            continue;
        }
        {
            auto pass = m_state.context->BeginComputePass({.buffers = {cache->visible.get(), cache->arguments.get()}});
            pass.BindPipeline(*m_pipeline);
            pass.BindTexture(0, occlusion ? *m_tiles : *m_noTiles, *m_sampler);
            pass.BindStorageBuffer(0, *cache->instances);
            pass.BindStorageBuffer(1, *cache->batchTable);
            pass.PushUniforms(0, uniforms);
            pass.Dispatch(batches);
        }
        for (DrawCommand& command : m_state.commands) {
            if (command.instanceBuffer == cache->instances.get()) {
                command.keptInstances = cache->visible.get();
                command.arguments = cache->arguments.get();
            }
        }
    }
}

void WorldCuller::UpdateOcclusionTiles() {
    m_valid = false;
    const Texture* depth = m_state.target.depthTarget;
    if (!m_tilePass || !m_pipeline || m_caches.empty() || !depth || depth->GetSampleCount() != 1) {
        return;
    }
    SPOT_PROFILE_SCOPE("UpdateOcclusionTiles");
    const std::uint32_t width = (depth->GetWidth() + OcclusionTileSize - 1) / OcclusionTileSize;
    const std::uint32_t height = (depth->GetHeight() + OcclusionTileSize - 1) / OcclusionTileSize;
    if (!m_tiles || m_tiles->GetWidth() != width || m_tiles->GetHeight() != height) {
        m_tiles = m_state.device->CreateTexture(
            {.width = width, .height = height, .format = TextureFormat::R32Float, .renderTarget = true});
        if (!m_tiles) {
            return;
        }
    }
    auto pass = m_state.context->BeginRenderPass({.target = m_tiles.get()});
    pass.PushFragmentUniforms(
        0, std::array<std::uint32_t, 4>{OcclusionTileSize, depth->GetWidth(), depth->GetHeight(), 0});
    const std::array inputs{FullscreenInput{depth, m_sampler.get()}};
    m_tilePass->Draw(pass, inputs);
    m_viewProjection = m_state.viewProjection;
    m_depth = depth;
    m_valid = true;
}

void WorldCuller::ReleaseUnused() {
    std::erase_if(m_caches, [this](const auto& cache) { return m_state.endCount - cache->lastDrawn > 120; });
}

} // namespace Spot::Detail
