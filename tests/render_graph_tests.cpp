#include <spot.h>

#include <catch2/catch_test_macros.hpp>

namespace {

const Spot::TextureDesc Color{
    .width = 64, .height = 64, .format = Spot::TextureFormat::Rgba16Float, .renderTarget = true};

} // namespace

TEST_CASE("Render graphs leave out passes whose results nothing uses", "[render_graph]") {
    Spot::RenderGraph graph;
    const auto target = graph.Import("Target", nullptr);
    Spot::RenderGraphTexture scene;
    Spot::RenderGraphTexture unused;
    graph.AddPass("Scene",
                  [&](Spot::RenderGraphBuilder& pass) {
                      scene = pass.Create("Scene", Color);
                      pass.Write(scene);
                  },
                  {});
    graph.AddPass("Unused",
                  [&](Spot::RenderGraphBuilder& pass) {
                      unused = pass.Create("Unused", Color);
                      pass.Read(scene);
                      pass.Write(unused);
                  },
                  {});
    graph.AddPass("Download",
                  [&](Spot::RenderGraphBuilder& pass) {
                      pass.Read(scene);
                      pass.KeepAlways();
                  },
                  {});
    graph.AddPass("Present",
                  [&](Spot::RenderGraphBuilder& pass) {
                      pass.Read(scene);
                      pass.Write(target);
                  },
                  {});
    graph.Compile();

    CHECK_FALSE(graph.IsCulled(0));
    CHECK(graph.IsCulled(1));
    CHECK_FALSE(graph.IsCulled(2));
    CHECK_FALSE(graph.IsCulled(3));
    CHECK(graph.GetStats().culledPasses == 1);
    CHECK(graph.GetPhysicalIndex(unused) == Spot::RenderGraphTexture::Invalid);
    CHECK(graph.GetPhysicalIndex(target) == Spot::RenderGraphTexture::Invalid);
}

TEST_CASE("Render graphs share textures between those not in use at the same time", "[render_graph]") {
    Spot::RenderGraph graph;
    const auto target = graph.Import("Target", nullptr);
    Spot::RenderGraphTexture a;
    Spot::RenderGraphTexture b;
    Spot::RenderGraphTexture c;
    Spot::RenderGraphTexture half;
    graph.AddPass("A",
                  [&](Spot::RenderGraphBuilder& pass) {
                      a = pass.Create("A", Color);
                      pass.Write(a);
                  },
                  {});
    graph.AddPass("B",
                  [&](Spot::RenderGraphBuilder& pass) {
                      b = pass.Create("B", Color);
                      pass.Read(a);
                      pass.Write(b);
                  },
                  {});
    // A is no longer used, so C can take its texture; the smaller one cannot.
    graph.AddPass(
        "C",
        [&](Spot::RenderGraphBuilder& pass) {
            c = pass.Create("C", Color);
            half = pass.Create(
                "Half", {.width = 32, .height = 32, .format = Spot::TextureFormat::Rgba16Float, .renderTarget = true});
            pass.Read(b);
            pass.Write(c);
            pass.Write(half);
        },
        {});
    graph.AddPass("Present",
                  [&](Spot::RenderGraphBuilder& pass) {
                      pass.Read(c);
                      pass.Read(half);
                      pass.Write(target);
                  },
                  {});
    graph.Compile();

    CHECK(graph.GetPhysicalIndex(a) == graph.GetPhysicalIndex(c));
    CHECK(graph.GetPhysicalIndex(a) != graph.GetPhysicalIndex(b));
    CHECK(graph.GetPhysicalIndex(half) != graph.GetPhysicalIndex(c));
    CHECK(graph.GetStats().transientTextures == 4);
    CHECK(graph.GetStats().physicalTextures == 3);
    CHECK(graph.GetDesc(half).width == 32);
}
