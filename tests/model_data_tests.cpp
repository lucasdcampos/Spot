#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using Spot::ModelData;
using Spot::Vector3;

namespace {

const std::filesystem::path ModelsDir = std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "models";

bool IsNear(Vector3 a, Vector3 b) {
    constexpr float Tolerance = 1e-4f;
    return std::abs(a.x - b.x) < Tolerance && std::abs(a.y - b.y) < Tolerance && std::abs(a.z - b.z) < Tolerance;
}

std::vector<std::byte> ReadBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> chars{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const auto bytes = std::as_bytes(std::span(chars));
    return {bytes.begin(), bytes.end()};
}

} // namespace

TEST_CASE("ModelData loads a .gltf with its separate buffer and image", "[model]") {
    const auto model = ModelData::Load(ModelsDir / "textured_quad.gltf");
    REQUIRE(model.has_value());

    REQUIRE(model->images.size() == 1);
    CHECK(model->images[0].GetWidth() == 2);

    REQUIRE(model->materials.size() == 1);
    const auto& material = model->materials[0];
    CHECK(material.name == "Tinted");
    CHECK(material.baseColor == Spot::Color{1.0f, 0.5f, 0.25f, 1.0f});
    CHECK(material.baseColorImage == 0);
    CHECK(material.doubleSided);

    REQUIRE(model->meshes.size() == 1);
    const auto& part = model->meshes[0];
    CHECK(part.name == "Quad");
    CHECK(part.material == 0);
    REQUIRE(part.mesh.vertices.size() == 4);
    CHECK(part.mesh.indices == std::vector<std::uint32_t>{0, 1, 2, 2, 3, 0});

    // The node's scale and translation are applied, and the missing normals are computed from the faces.
    CHECK(IsNear(part.mesh.vertices[0].position, {-2.0f, -2.0f, -2.0f}));
    CHECK(IsNear(part.mesh.vertices[2].position, {2.0f, 2.0f, -2.0f}));
    CHECK(part.mesh.vertices[0].uv == Spot::Vector2{0.0f, 1.0f});
    for (const auto& vertex : part.mesh.vertices) {
        CHECK(IsNear(vertex.normal, {0.0f, 0.0f, 1.0f}));
    }
}

TEST_CASE("ModelData loads a .glb with an embedded image and a node hierarchy", "[model]") {
    const auto model = ModelData::Load(ModelsDir / "cube.glb");
    REQUIRE(model.has_value());

    REQUIRE(model->images.size() == 1);
    REQUIRE(model->materials.size() == 2);
    CHECK(model->materials[0].baseColorImage == 0);
    CHECK(model->materials[0].baseColor == Spot::Color::White);
    CHECK_FALSE(model->materials[1].baseColorImage.has_value());
    CHECK(model->materials[1].baseColor == Spot::Color::Blue);
    CHECK_FALSE(model->materials[1].doubleSided);

    // One mesh per material, each with three faces of the cube.
    REQUIRE(model->meshes.size() == 2);
    CHECK(model->meshes[0].material == 0);
    CHECK(model->meshes[1].material == 1);

    // The child sits one unit along +Z of its parent, which is at x = 10 and turned a quarter turn around Y, so the
    // cube is centered at (11, 0, 0) with its +Z face now facing +X.
    const Vector3 center{11.0f, 0.0f, 0.0f};
    for (const auto& part : model->meshes) {
        CHECK(part.mesh.vertices.size() == 12);
        CHECK(part.mesh.indices.size() == 18);
        for (const auto& vertex : part.mesh.vertices) {
            CHECK(std::abs(Spot::Dot(vertex.position - center, vertex.normal) - 0.5f) < 1e-4f);
        }
    }
    CHECK(IsNear(model->meshes[0].mesh.vertices[0].normal, {1.0f, 0.0f, 0.0f}));
}

TEST_CASE("ModelData loads a self-contained .gltf from memory", "[model]") {
    const auto bytes = ReadBytes(ModelsDir / "embedded_triangle.gltf");
    REQUIRE_FALSE(bytes.empty());

    const auto model = ModelData::LoadFromMemory(bytes);
    REQUIRE(model.has_value());
    CHECK(model->images.size() == 1);
    CHECK(model->materials.empty());
    REQUIRE(model->meshes.size() == 1);

    // Without indices, the vertices are drawn in order.
    const auto& part = model->meshes[0];
    CHECK_FALSE(part.material.has_value());
    CHECK(part.mesh.indices == std::vector<std::uint32_t>{0, 1, 2});
    CHECK(IsNear(part.mesh.vertices[1].position, {1.0f, 0.0f, 0.0f}));
    CHECK(IsNear(part.mesh.vertices[1].normal, {0.0f, 0.0f, 1.0f}));
}

TEST_CASE("ModelData rejects missing files and invalid data", "[model]") {
    CHECK_FALSE(ModelData::Load(ModelsDir / "missing.glb").has_value());

    const std::array<std::byte, 4> garbage{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    CHECK_FALSE(ModelData::LoadFromMemory(garbage).has_value());

    // From memory there is no folder to find the separate buffer in.
    CHECK_FALSE(ModelData::LoadFromMemory(ReadBytes(ModelsDir / "textured_quad.gltf")).has_value());
}
