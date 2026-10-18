#pragma once

#include "graphics/texture.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Spot {

class GraphicsDevice;
class RenderContext;
class RenderGraph;

// A texture of a render graph, valid for the frame it was made in: one imported from outside, or one the graph
// creates for the frame and lends from textures it keeps.
struct RenderGraphTexture {
    static constexpr std::uint32_t Invalid = ~0u;
    std::uint32_t index = Invalid;

    bool IsValid() const { return index != Invalid; }
    bool operator==(const RenderGraphTexture&) const = default;
};

// What a pass declares, while being added, about the textures it uses.
class RenderGraphBuilder {
public:
    // A texture that lives for the frame only; its contents are undefined until a pass draws into it.
    RenderGraphTexture Create(std::string_view name, const TextureDesc& desc);
    void Read(RenderGraphTexture texture);
    // Passes that draw over what a texture holds read it too.
    void Write(RenderGraphTexture texture);
    // Keeps the pass even when nothing reads what it writes, for effects the graph cannot see, such as downloads.
    void KeepAlways();

private:
    friend class RenderGraph;
    RenderGraphBuilder(RenderGraph& graph, std::uint32_t pass) : m_graph(graph), m_pass(pass) {}

    RenderGraph& m_graph;
    std::uint32_t m_pass;
};

// What a pass runs with.
class RenderGraphResources {
public:
    // The texture behind a handle the pass declared; null for an imported frame target (the window), as render
    // passes take it.
    Texture* GetTexture(RenderGraphTexture texture) const;
    RenderContext& GetContext() const { return m_context; }

private:
    friend class RenderGraph;
    RenderGraphResources(const RenderGraph& graph, RenderContext& context) : m_graph(graph), m_context(context) {}

    const RenderGraph& m_graph;
    RenderContext& m_context;
};

struct RenderGraphStats {
    std::uint32_t passes = 0;
    // Left out because nothing kept read what they wrote.
    std::uint32_t culledPasses = 0;
    // Created by passes, and the textures that held them, fewer when textures that are not used at the same time
    // share one.
    std::uint32_t transientTextures = 0;
    std::uint32_t physicalTextures = 0;
    // Created this frame because none kept from earlier frames matched.
    std::uint32_t createdTextures = 0;
};

// A frame described as passes that declare the textures they read and write, then run in the order they were added.
// Before running them the graph leaves out passes whose results nothing uses, and backs the textures that live for
// the frame only with textures it keeps from frame to frame, the same one for textures of the same description that
// are not in use at the same time:
//
//     Spot::RenderGraph graph;
//     auto target = graph.Import("Window", nullptr);
//     Spot::RenderGraphTexture scene;
//     graph.AddPass("Scene", [&](Spot::RenderGraphBuilder& pass) {
//         scene = pass.Create("Scene", {.width = w, .height = h, .format = format, .renderTarget = true});
//         pass.Write(scene);
//     }, [&](Spot::RenderGraphResources& resources) { ... draw into resources.GetTexture(scene) ... });
//     graph.AddPass("Present", [&](Spot::RenderGraphBuilder& pass) { pass.Read(scene); pass.Write(target); },
//                   [&](Spot::RenderGraphResources& resources) { ... });
//     graph.Execute(device, context);
//
// Writing an imported texture is what keeps passes: they are the frame's results. Backends order the GPU's work on
// textures themselves, so the graph has no barriers to place.
class RenderGraph {
public:
    using Setup = std::function<void(RenderGraphBuilder&)>;
    using Run = std::function<void(RenderGraphResources&)>;

    RenderGraph();
    ~RenderGraph();

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // A texture from outside the graph, or null for the frame target.
    RenderGraphTexture Import(std::string_view name, Texture* texture);
    // Runs setup now, so later passes can use what it creates.
    void AddPass(std::string_view name, const Setup& setup, Run run);
    const TextureDesc& GetDesc(RenderGraphTexture texture) const;

    // Decides which passes run and which textures share, without running anything; Execute() does it first.
    void Compile();
    bool IsCulled(std::uint32_t pass) const;
    // The texture a transient one is held in this frame, the same for those sharing one; Invalid for imported ones.
    std::uint32_t GetPhysicalIndex(RenderGraphTexture texture) const;

    // Runs the passes kept, then forgets them and their textures, so the next frame adds its own. Textures it kept
    // and no frame used for a few frames are released.
    void Execute(GraphicsDevice& device, RenderContext& context);

    RenderGraphStats GetStats() const { return m_stats; }

private:
    friend class RenderGraphBuilder;
    friend class RenderGraphResources;

    struct Resource {
        std::string name;
        TextureDesc desc;
        bool imported = false;
        Texture* texture = nullptr;
        // Of the passes kept that use it, for transient ones.
        std::uint32_t firstUse = ~0u;
        std::uint32_t lastUse = 0;
        std::uint32_t physical = ~0u;
    };
    struct Pass {
        std::string name;
        Run run;
        std::vector<std::uint32_t> reads;
        std::vector<std::uint32_t> writes;
        bool keepAlways = false;
        bool culled = false;
    };
    struct Physical {
        TextureDesc desc;
        std::uint32_t lastUse = 0;
        Texture* texture = nullptr;
    };
    struct Kept {
        TextureDesc desc;
        std::unique_ptr<Texture> texture;
        std::uint64_t lastFrame = 0;
        bool taken = false;
    };

    std::vector<Resource> m_resources;
    std::vector<Pass> m_passes;
    std::vector<Physical> m_physical;
    std::vector<Kept> m_kept;
    std::uint64_t m_frame = 0;
    bool m_compiled = false;
    RenderGraphStats m_stats;
};

} // namespace Spot
