#include "graphics/render_graph.h"

#include "core/assert.h"
#include "core/log.h"
#include "core/profiler.h"
#include "graphics/graphics_device.h"

#include <algorithm>

namespace Spot {

namespace {

// Kept textures no frame used for this many frames are released.
constexpr std::uint64_t KeptFrames = 3;

bool SameDesc(const TextureDesc& a, const TextureDesc& b) {
    return a.width == b.width && a.height == b.height && a.format == b.format && a.type == b.type &&
           a.renderTarget == b.renderTarget && a.mipmaps == b.mipmaps && a.sampleCount == b.sampleCount &&
           a.storage == b.storage;
}

} // namespace

RenderGraphTexture RenderGraphBuilder::Create(std::string_view name, const TextureDesc& desc) {
    const auto index = static_cast<std::uint32_t>(m_graph.m_resources.size());
    m_graph.m_resources.push_back({.name = std::string(name), .desc = desc});
    return {index};
}

void RenderGraphBuilder::Read(RenderGraphTexture texture) {
    SPOT_ASSERT(texture.index < m_graph.m_resources.size(), "Not a texture of this graph");
    m_graph.m_passes[m_pass].reads.push_back(texture.index);
}

void RenderGraphBuilder::Write(RenderGraphTexture texture) {
    SPOT_ASSERT(texture.index < m_graph.m_resources.size(), "Not a texture of this graph");
    m_graph.m_passes[m_pass].writes.push_back(texture.index);
}

void RenderGraphBuilder::KeepAlways() {
    m_graph.m_passes[m_pass].keepAlways = true;
}

Texture* RenderGraphResources::GetTexture(RenderGraphTexture texture) const {
    SPOT_ASSERT(texture.index < m_graph.m_resources.size(), "Not a texture of this graph");
    return m_graph.m_resources[texture.index].texture;
}

RenderGraph::RenderGraph() = default;
RenderGraph::~RenderGraph() = default;

RenderGraphTexture RenderGraph::Import(std::string_view name, Texture* texture) {
    const auto index = static_cast<std::uint32_t>(m_resources.size());
    Resource resource{.name = std::string(name), .imported = true, .texture = texture};
    if (texture) {
        resource.desc = {.width = texture->GetWidth(),
                         .height = texture->GetHeight(),
                         .format = texture->GetFormat(),
                         .type = texture->GetType(),
                         .renderTarget = texture->IsRenderTarget(),
                         .mipmaps = texture->GetMipLevelCount() > 1,
                         .storage = texture->IsStorage()};
    }
    m_resources.push_back(std::move(resource));
    m_compiled = false;
    return {index};
}

void RenderGraph::AddPass(std::string_view name, const Setup& setup, Run run) {
    const auto index = static_cast<std::uint32_t>(m_passes.size());
    m_passes.push_back({.name = std::string(name), .run = std::move(run)});
    RenderGraphBuilder builder(*this, index);
    if (setup) {
        setup(builder);
    }
    m_compiled = false;
}

const TextureDesc& RenderGraph::GetDesc(RenderGraphTexture texture) const {
    SPOT_ASSERT(texture.index < m_resources.size(), "Not a texture of this graph");
    return m_resources[texture.index].desc;
}

void RenderGraph::Compile() {
    if (m_compiled) {
        return;
    }
    m_compiled = true;
    m_stats = {.passes = static_cast<std::uint32_t>(m_passes.size())};

    // From the last pass back: a pass is kept when it writes what the frame shows or what a kept pass reads.
    std::vector<bool> needed(m_resources.size(), false);
    for (std::size_t i = m_passes.size(); i-- > 0;) {
        Pass& pass = m_passes[i];
        const bool used = pass.keepAlways || std::ranges::any_of(pass.writes, [&](std::uint32_t resource) {
                              return m_resources[resource].imported || needed[resource];
                          });
        pass.culled = !used;
        if (pass.culled) {
            ++m_stats.culledPasses;
            continue;
        }
        for (const std::uint32_t resource : pass.reads) {
            needed[resource] = true;
        }
    }

    for (Resource& resource : m_resources) {
        resource.firstUse = ~0u;
        resource.lastUse = 0;
        resource.physical = ~0u;
    }
    for (std::uint32_t i = 0; i < m_passes.size(); ++i) {
        if (m_passes[i].culled) {
            continue;
        }
        const auto use = [&](std::uint32_t index) {
            Resource& resource = m_resources[index];
            resource.firstUse = std::min(resource.firstUse, i);
            resource.lastUse = std::max(resource.lastUse, i);
        };
        std::ranges::for_each(m_passes[i].reads, use);
        std::ranges::for_each(m_passes[i].writes, use);
    }

    // Transient textures in the order they start being used take the first physical one of their description that
    // is free by then.
    std::vector<std::uint32_t> order;
    for (std::uint32_t i = 0; i < m_resources.size(); ++i) {
        if (!m_resources[i].imported && m_resources[i].firstUse != ~0u) {
            order.push_back(i);
        }
    }
    std::ranges::stable_sort(order, {}, [this](std::uint32_t i) { return m_resources[i].firstUse; });
    m_physical.clear();
    for (const std::uint32_t index : order) {
        Resource& resource = m_resources[index];
        ++m_stats.transientTextures;
        auto found = std::ranges::find_if(m_physical, [&](const Physical& physical) {
            return physical.lastUse < resource.firstUse && SameDesc(physical.desc, resource.desc);
        });
        if (found == m_physical.end()) {
            m_physical.push_back({.desc = resource.desc});
            found = m_physical.end() - 1;
        }
        found->lastUse = resource.lastUse;
        resource.physical = static_cast<std::uint32_t>(found - m_physical.begin());
    }
    m_stats.physicalTextures = static_cast<std::uint32_t>(m_physical.size());
}

bool RenderGraph::IsCulled(std::uint32_t pass) const {
    SPOT_ASSERT(pass < m_passes.size(), "Not a pass of this graph");
    return m_passes[pass].culled;
}

std::uint32_t RenderGraph::GetPhysicalIndex(RenderGraphTexture texture) const {
    SPOT_ASSERT(texture.index < m_resources.size(), "Not a texture of this graph");
    return m_resources[texture.index].physical;
}

void RenderGraph::Execute(GraphicsDevice& device, RenderContext& context) {
    SPOT_PROFILE_SCOPE("RenderGraph::Execute");
    Compile();
    ++m_frame;

    for (Kept& kept : m_kept) {
        kept.taken = false;
    }
    for (Physical& physical : m_physical) {
        auto kept = std::ranges::find_if(
            m_kept, [&](const Kept& candidate) { return !candidate.taken && SameDesc(candidate.desc, physical.desc); });
        if (kept == m_kept.end()) {
            std::unique_ptr<Texture> texture = device.CreateTexture(physical.desc);
            if (texture) {
                ++m_stats.createdTextures;
                m_kept.push_back({.desc = physical.desc, .texture = std::move(texture)});
                kept = m_kept.end() - 1;
            }
        }
        if (kept != m_kept.end()) {
            kept->taken = true;
            kept->lastFrame = m_frame;
            physical.texture = kept->texture.get();
        } else {
            physical.texture = nullptr;
        }
    }
    for (Resource& resource : m_resources) {
        if (!resource.imported) {
            resource.texture = resource.physical < m_physical.size() ? m_physical[resource.physical].texture : nullptr;
        }
    }

    RenderGraphResources resources(*this, context);
    for (Pass& pass : m_passes) {
        if (pass.culled || !pass.run) {
            continue;
        }
        // A transient texture that could not be created is null, which passes would take for the frame target.
        const auto missing = [this](std::uint32_t index) {
            return !m_resources[index].imported && !m_resources[index].texture;
        };
        if (std::ranges::any_of(pass.reads, missing) || std::ranges::any_of(pass.writes, missing)) {
            Log::GetEngineLogger().Error("Render graph pass '{}' skipped: a texture it uses could not be created",
                                         pass.name);
            continue;
        }
        SPOT_PROFILE_SCOPE(Profiler::Intern(pass.name));
        pass.run(resources);
    }

    m_passes.clear();
    m_resources.clear();
    m_physical.clear();
    m_compiled = false;
    std::erase_if(m_kept, [this](const Kept& kept) { return m_frame - kept.lastFrame >= KeptFrames; });
}

} // namespace Spot
