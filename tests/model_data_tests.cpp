#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include <string>

#include <span>

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

namespace {

Spot::Vector3 Xyz(Spot::Vector4 v) {
    return {v.x, v.y, v.z};
}

// The direction toward the texture's top, from a vertex's normal and tangent.
Spot::Vector3 Bitangent(const Spot::MeshVertex& vertex) {
    return Spot::Cross(vertex.normal, Xyz(vertex.tangent)) * vertex.tangent.w;
}

std::string Base64(std::span<const std::byte> bytes) {
    constexpr std::string_view Digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string text;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        std::uint32_t chunk = std::to_integer<std::uint32_t>(bytes[i]) << 16;
        if (i + 1 < bytes.size()) {
            chunk |= std::to_integer<std::uint32_t>(bytes[i + 1]) << 8;
        }
        if (i + 2 < bytes.size()) {
            chunk |= std::to_integer<std::uint32_t>(bytes[i + 2]);
        }
        text += Digits[(chunk >> 18) & 63];
        text += Digits[(chunk >> 12) & 63];
        text += i + 1 < bytes.size() ? Digits[(chunk >> 6) & 63] : '=';
        text += i + 2 < bytes.size() ? Digits[chunk & 63] : '=';
    }
    return text;
}

} // namespace

TEST_CASE("Built meshes get tangents along their texture's u, on the side of its top", "[mesh]") {
    const Spot::MeshData plane = Spot::MeshData::CreatePlane(2.0f);
    for (const Spot::MeshVertex& vertex : plane.vertices) {
        CHECK(Spot::Distance(Xyz(vertex.tangent), Spot::Vector3{1.0f, 0.0f, 0.0f}) < 1e-4f);
        // v grows toward +Z, so the texture's top is toward -Z.
        CHECK(Spot::Distance(Bitangent(vertex), Spot::Vector3{0.0f, 0.0f, -1.0f}) < 1e-4f);
    }

    for (const Spot::MeshData& mesh : {Spot::MeshData::CreateCube(), Spot::MeshData::CreateSphere(0.5f, 12, 6)}) {
        for (const Spot::MeshVertex& vertex : mesh.vertices) {
            CHECK(std::abs(Spot::Length(Xyz(vertex.tangent)) - 1.0f) < 1e-3f);
            CHECK(std::abs(Spot::Dot(Xyz(vertex.tangent), vertex.normal)) < 1e-3f);
            CHECK(std::abs(vertex.tangent.w) == 1.0f);
        }
    }
    // The cube's front face maps u to the right.
    const Spot::MeshData cube = Spot::MeshData::CreateCube();
    CHECK(Spot::Distance(Xyz(cube.vertices[0].tangent), Spot::Vector3{1.0f, 0.0f, 0.0f}) < 1e-4f);
}

TEST_CASE("Tangents split vertices whose triangles need different ones", "[mesh]") {
    // Two triangles sharing an edge, the second with its texture mirrored, as across a mirrored seam.
    Spot::MeshData mesh;
    const Spot::Vector3 up{0.0f, 1.0f, 0.0f};
    mesh.vertices = {{{0.0f, 0.0f, 0.0f}, up, {0.0f, 0.0f}},
                     {{0.0f, 0.0f, 1.0f}, up, {0.0f, 1.0f}},
                     {{1.0f, 0.0f, 0.0f}, up, {1.0f, 0.0f}},
                     {{-1.0f, 0.0f, 0.0f}, up, {1.0f, 0.0f}}};
    mesh.indices = {0, 1, 2, 0, 3, 1};
    mesh.GenerateTangents();
    CHECK(mesh.vertices.size() == 6);
    CHECK(mesh.indices.size() == 6);
    // Each triangle's corners agree on their tangent, and the two triangles' tangents point apart.
    const Spot::Vector3 first = Xyz(mesh.vertices[mesh.indices[0]].tangent);
    const Spot::Vector3 second = Xyz(mesh.vertices[mesh.indices[3]].tangent);
    CHECK(Spot::Dot(first, Xyz(mesh.vertices[mesh.indices[2]].tangent)) > 0.99f);
    CHECK(Spot::Dot(first, second) < -0.99f);
}

TEST_CASE("Tangents a glTF file gives are used, turned with their node", "[mesh][model]") {
    // One triangle facing +Z, whose tangent points along +Y with its bitangent on the negative side.
    struct Vertex {
        float position[3];
        float normal[3];
        float uv[2];
        float tangent[4];
    };
    const std::array<Vertex, 3> vertices{{
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, -1.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, -1.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, -1.0f}},
    }};
    const auto bytes = std::as_bytes(std::span(vertices));
    const std::string gltf = std::string(R"({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "rotation": [0, 0, 0.7071068, 0.7071068]}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "TANGENT": 3}}]}],
        "buffers": [{"byteLength": )") +
                             std::to_string(bytes.size()) + R"(, "uri": "data:application/octet-stream;base64,)" +
                             Base64(bytes) + R"("}],
        "bufferViews": [{"buffer": 0, "byteLength": )" +
                             std::to_string(bytes.size()) + R"(, "byteStride": 48}],
        "accessors": [
            {"bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": 3, "type": "VEC3",
             "min": [0, 0, 0], "max": [1, 1, 0]},
            {"bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 0, "byteOffset": 24, "componentType": 5126, "count": 3, "type": "VEC2"},
            {"bufferView": 0, "byteOffset": 32, "componentType": 5126, "count": 3, "type": "VEC4"}]})";
    const auto model = Spot::ModelData::LoadFromMemory(std::as_bytes(std::span(gltf)));
    REQUIRE(model.has_value());
    REQUIRE(model->meshes.size() == 1);
    const Spot::MeshVertex& vertex = model->meshes[0].mesh.vertices[0];
    // The node turns it a quarter around Z, taking +Y to -X.
    CHECK(Spot::Distance(Xyz(vertex.tangent), Spot::Vector3{-1.0f, 0.0f, 0.0f}) < 1e-4f);
    CHECK(vertex.tangent.w == -1.0f);
}

TEST_CASE("ModelData reads every PBR input of glTF materials, and which images hold colors", "[model]") {
    const auto png = [](std::uint8_t value) {
        return Base64(Spot::Image(1, 1, {value, value, value, 255}).EncodePng());
    };
    const std::string gltf = R"({"asset": {"version": "2.0"}, "extensionsUsed": ["KHR_materials_emissive_strength"],
        "images": [{"uri": "data:image/png;base64,)" +
                             png(200) + R"("}, {"uri": "data:image/png;base64,)" + png(100) + R"("}],
        "textures": [{"source": 0}, {"source": 1}],
        "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.25,
                                                "roughnessFactor": 0.75, "metallicRoughnessTexture": {"index": 1}},
                       "normalTexture": {"index": 1, "scale": 0.5},
                       "occlusionTexture": {"index": 1, "strength": 0.8},
                       "emissiveFactor": [1, 0.5, 0], "emissiveTexture": {"index": 0},
                       "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 4}}},
                      {}]})";
    const auto model = Spot::ModelData::LoadFromMemory(std::as_bytes(std::span(gltf)));
    REQUIRE(model.has_value());
    REQUIRE(model->images.size() == 2);
    REQUIRE(model->materials.size() == 2);
    const Spot::MaterialData& material = model->materials[0];
    CHECK(material.baseColorImage == 0u);
    CHECK(material.metallic == 0.25f);
    CHECK(material.roughness == 0.75f);
    CHECK(material.metallicRoughnessImage == 1u);
    CHECK(material.normalImage == 1u);
    CHECK(material.normalScale == 0.5f);
    CHECK(material.occlusionImage == 1u);
    CHECK(material.occlusionStrength == Catch::Approx(0.8f));
    CHECK(material.emissive == Spot::Color{4.0f, 2.0f, 0.0f, 1.0f});
    CHECK(material.emissiveImage == 0u);

    // glTF's defaults: metallic and matte, with nothing emitted.
    const Spot::MaterialData& plain = model->materials[1];
    CHECK(plain.metallic == 1.0f);
    CHECK(plain.roughness == 1.0f);
    CHECK(plain.emissive == Spot::Color::Black);
    CHECK_FALSE(plain.normalImage.has_value());

    CHECK(model->IsColorImage(0));
    CHECK_FALSE(model->IsColorImage(1));
}
