#include "textured.frag.h"
#include "triangle.vert.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

TEST_CASE("Shaders are compiled to SPIR-V and Metal source", "[shader]") {
    const auto& shader = Shaders::TriangleVert;

    CHECK(shader.stage == Spot::ShaderStage::Vertex);
    // Every SPIR-V module starts with this magic number, stored little-endian.
    REQUIRE(shader.spirv.size() > 4);
    CHECK(shader.spirv[0] == 0x03);
    CHECK(shader.spirv[1] == 0x02);
    CHECK(shader.spirv[2] == 0x23);
    CHECK(shader.spirv[3] == 0x07);

    const std::string_view msl(reinterpret_cast<const char*>(shader.msl.data()), shader.msl.size());
    CHECK(msl.find("vertex") != std::string_view::npos);
}

TEST_CASE("Shaders include DXIL when the build has DXC", "[shader]") {
#ifdef SPOT_TEST_EXPECT_DXIL
    CHECK_FALSE(Shaders::TriangleVert.dxil.empty());
#else
    CHECK(Shaders::TriangleVert.dxil.empty());
#endif
}

TEST_CASE("Shader resource counts come from reflection", "[shader]") {
    const auto& shader = Shaders::TexturedFrag;

    CHECK(shader.stage == Spot::ShaderStage::Fragment);
    CHECK(shader.samplerCount == 1);
    CHECK(shader.uniformBufferCount == 1);
    CHECK(shader.storageTextureCount == 0);
    CHECK(shader.storageBufferCount == 0);
    CHECK(Shaders::TriangleVert.samplerCount == 0);
    CHECK(Shaders::TriangleVert.uniformBufferCount == 0);
}
