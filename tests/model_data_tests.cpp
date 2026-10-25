#include <spot.h>
#include <spot/assets/primitive_meshes.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
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
    CHECK(part.materials == std::vector<std::optional<std::size_t>>{0});
    REQUIRE(part.mesh.vertices.size() == 4);
    CHECK(part.mesh.indices == std::vector<std::uint32_t>{0, 1, 2, 2, 3, 0});

    // The mesh stays in its own space, which its node scales and moves; the missing normals are computed from the
    // faces.
    REQUIRE(model->nodes.size() == 1);
    CHECK(model->nodes[0].name == "Quad");
    CHECK(model->nodes[0].mesh == 0u);
    const Spot::Matrix4 world = model->GetWorldTransform(0);
    const auto placed = [&](Vector3 p) {
        const Spot::Vector4 v = world * Spot::Vector4{p.x, p.y, p.z, 1.0f};
        return Vector3{v.x, v.y, v.z};
    };
    CHECK(IsNear(placed(part.mesh.vertices[0].position), {-2.0f, -2.0f, -2.0f}));
    CHECK(IsNear(placed(part.mesh.vertices[2].position), {2.0f, 2.0f, -2.0f}));
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

    // One mesh with a submesh per material, each with three faces of the cube.
    REQUIRE(model->meshes.size() == 1);
    const auto& cube = model->meshes[0];
    CHECK(cube.materials == std::vector<std::optional<std::size_t>>{0, 1});
    CHECK(cube.mesh.vertices.size() == 24);
    CHECK(cube.mesh.submeshes == std::vector<Spot::SubMesh>{{0, 18}, {18, 18}});

    // The nodes keep their names and hierarchy: the child sits one unit along +Z of its parent, which is at x = 10
    // and turned a quarter turn around Y, so the cube is centered at (11, 0, 0) with its +Z face now facing +X.
    REQUIRE(model->nodes.size() == 2);
    CHECK(model->nodes[0].name == "Pivot");
    CHECK_FALSE(model->nodes[0].parent.has_value());
    CHECK_FALSE(model->nodes[0].mesh.has_value());
    CHECK(model->nodes[1].name == "Cube");
    CHECK(model->nodes[1].parent == 0u);
    CHECK(model->nodes[1].mesh == 0u);
    const Spot::Matrix4 world = model->GetWorldTransform(1);
    const Vector3 center{11.0f, 0.0f, 0.0f};
    for (const auto& vertex : cube.mesh.vertices) {
        const Spot::Vector4 p = world * Spot::Vector4{vertex.position.x, vertex.position.y, vertex.position.z, 1.0f};
        const Spot::Vector4 n = world * Spot::Vector4{vertex.normal.x, vertex.normal.y, vertex.normal.z, 0.0f};
        CHECK(std::abs(Spot::Dot(Vector3{p.x, p.y, p.z} - center, Vector3{n.x, n.y, n.z}) - 0.5f) < 1e-4f);
    }
    const Spot::Vector4 first = world * Spot::Vector4{cube.mesh.vertices[0].normal.x, cube.mesh.vertices[0].normal.y,
                                                      cube.mesh.vertices[0].normal.z, 0.0f};
    CHECK(IsNear({first.x, first.y, first.z}, {1.0f, 0.0f, 0.0f}));
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
    CHECK(part.materials == std::vector<std::optional<std::size_t>>{std::nullopt});
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
    const Spot::MeshData plane = Spot::PrimitiveMeshes::CreatePlane(2.0f);
    for (const Spot::MeshVertex& vertex : plane.vertices) {
        CHECK(Spot::Distance(Xyz(vertex.tangent), Spot::Vector3{1.0f, 0.0f, 0.0f}) < 1e-4f);
        // v grows toward +Z, so the texture's top is toward -Z.
        CHECK(Spot::Distance(Bitangent(vertex), Spot::Vector3{0.0f, 0.0f, -1.0f}) < 1e-4f);
    }

    for (const Spot::MeshData& mesh :
         {Spot::PrimitiveMeshes::CreateCube(), Spot::PrimitiveMeshes::CreateSphere(0.5f, 12, 6)}) {
        for (const Spot::MeshVertex& vertex : mesh.vertices) {
            CHECK(std::abs(Spot::Length(Xyz(vertex.tangent)) - 1.0f) < 1e-3f);
            CHECK(std::abs(Spot::Dot(Xyz(vertex.tangent), vertex.normal)) < 1e-3f);
            CHECK(std::abs(vertex.tangent.w) == 1.0f);
        }
    }
    // The cube's front face maps u to the right.
    const Spot::MeshData cube = Spot::PrimitiveMeshes::CreateCube();
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

TEST_CASE("Tangents of meshes with many triangles of no area take no longer than without them", "[mesh]") {
    // Blades of grass, as strips of quads up to a tip as wide as the given width: none makes the last triangle of
    // each blade one with two corners in one place.
    const auto blades = [](float tipWidth) {
        Spot::MeshData mesh;
        for (int blade = 0; blade < 10000; ++blade) {
            const Vector3 root{static_cast<float>(blade % 100) * 0.1f, 0.0f, static_cast<float>(blade / 100) * 0.1f};
            const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
            for (int segment = 0; segment <= 4; ++segment) {
                const float u = static_cast<float>(segment) / 4.0f;
                const float halfWidth = (0.01f * (1.0f - u) + tipWidth * u) * 0.5f;
                const Vector3 center = root + Vector3{0.0f, 0.3f * u, 0.0f};
                mesh.vertices.push_back({center - Vector3{halfWidth, 0, 0}, {0.0f, 0.0f, 1.0f}, {0.0f, u}});
                mesh.vertices.push_back({center + Vector3{halfWidth, 0, 0}, {0.0f, 0.0f, 1.0f}, {1.0f, u}});
                if (segment < 4) {
                    const auto a = first + static_cast<std::uint32_t>(segment * 2);
                    mesh.indices.insert(mesh.indices.end(), {a, a + 1, a + 2, a + 1, a + 3, a + 2});
                }
            }
        }
        return mesh;
    };
    const auto time = [](Spot::MeshData& mesh) {
        const auto start = std::chrono::steady_clock::now();
        mesh.GenerateTangents();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    Spot::MeshData pointed = blades(0.0f);
    Spot::MeshData blunt = blades(0.001f);
    const double bluntTime = time(blunt);
    const double pointedTime = time(pointed);
    // MikkTSpace alone searches every triangle for each one of no area, which takes seconds here.
    CHECK(pointedTime < bluntTime * 3.0 + 0.2);

    // Every vertex still gets a tangent along the surface, the blades' u, but where only a pointless triangle used it.
    for (std::size_t i = 0; i < pointed.vertices.size(); ++i) {
        const Spot::MeshVertex& vertex = pointed.vertices[i];
        CHECK(std::abs(Spot::Length(Xyz(vertex.tangent)) - 1.0f) < 1e-3f);
        CHECK(std::abs(Spot::Dot(Xyz(vertex.tangent), vertex.normal)) < 1e-3f);
    }
    CHECK(Spot::Dot(Xyz(pointed.vertices[pointed.indices[0]].tangent), Vector3{1.0f, 0.0f, 0.0f}) > 0.99f);
    CHECK(Spot::Dot(Xyz(pointed.vertices[8].tangent), Vector3{1.0f, 0.0f, 0.0f}) > 0.99f);
}

TEST_CASE("Tangents a glTF file gives stay in mesh space", "[mesh][model]") {
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
    CHECK(IsNear(Xyz(vertex.tangent), {0.0f, 1.0f, 0.0f}));
    // Rendering turns it with the node, taking +Y to -X.
    const Spot::Vector4 turned = model->GetWorldTransform(0) * Spot::Vector4{0.0f, 1.0f, 0.0f, 0.0f};
    CHECK(IsNear(Xyz(turned), {-1.0f, 0.0f, 0.0f}));
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

TEST_CASE("ModelData reads glTF alpha modes", "[model]") {
    const std::string gltf = R"({"asset": {"version": "2.0"},
        "materials": [{"alphaMode": "MASK", "alphaCutoff": 0.25}, {"alphaMode": "BLEND"}, {}]})";
    const auto model = Spot::ModelData::LoadFromMemory(std::as_bytes(std::span(gltf)));
    REQUIRE(model.has_value());
    REQUIRE(model->materials.size() == 3);
    CHECK(model->materials[0].alphaMode == Spot::AlphaMode::Cutout);
    CHECK(model->materials[0].alphaCutoff == 0.25f);
    CHECK(model->materials[1].alphaMode == Spot::AlphaMode::Transparent);
    CHECK(model->materials[2].alphaMode == Spot::AlphaMode::Opaque);
    CHECK(model->materials[2].alphaCutoff == 0.5f);
}

TEST_CASE("Model nodes share meshes and keep empty nodes and multiple roots", "[model]") {
    const std::array<Vector3, 3> positions{{{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {0.0f, 0.5f, 0.0f}}};
    const std::string gltf = R"({"asset":{"version":"2.0"}, "scene":0, "scenes":[{"nodes":[0,3]}],
        "nodes":[{"name":"Pivot", "translation":[2,0,0], "children":[1,2]},
                 {"name":"First", "mesh":0, "translation":[0,1,0]},
                 {"name":"Second", "mesh":0, "scale":[-2,1,1]}, {"name":"Empty"}],
        "meshes":[{"name":"Shared", "primitives":[{"attributes":{"POSITION":0}},
                                                     {"attributes":{"POSITION":0}}]}],
        "buffers":[{"byteLength":36, "uri":"data:application/octet-stream;base64,)" +
                             Base64(std::as_bytes(std::span(positions))) + R"("}],
        "bufferViews":[{"buffer":0,"byteLength":36}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",
                      "min":[-0.5,-0.5,0],"max":[0.5,0.5,0]}]})";
    const auto model = ModelData::LoadFromMemory(std::as_bytes(std::span(gltf)));
    REQUIRE(model);
    REQUIRE(model->nodes.size() == 4);
    REQUIRE(model->meshes.size() == 1);
    CHECK(model->nodes[0].name == "Pivot");
    CHECK(model->nodes[1].parent == 0);
    CHECK(model->nodes[2].parent == 0);
    CHECK(model->nodes[1].mesh == model->nodes[2].mesh);
    CHECK_FALSE(model->nodes[3].parent);
    CHECK_FALSE(model->nodes[3].mesh);
    CHECK(model->meshes[0].mesh.submeshes == std::vector<Spot::SubMesh>{{0, 3}, {3, 3}});
    CHECK(IsNear(Xyz(model->GetWorldTransform(1).columns[3]), {2.0f, 1.0f, 0.0f}));
    CHECK(IsNear(Xyz(model->GetWorldTransform(2).columns[0]), {-2.0f, 0.0f, 0.0f}));
    CHECK(IsNear(model->meshes[0].mesh.vertices[0].position, positions[0]));
}

TEST_CASE("Appending submeshes offsets their vertices and keeps existing triangles", "[mesh]") {
    Spot::MeshData combined = Spot::PrimitiveMeshes::CreateCube();
    const Spot::MeshData second = Spot::PrimitiveMeshes::CreatePlane();
    const auto vertexCount = combined.vertices.size();
    const auto indexCount = combined.indices.size();
    combined.AppendSubmesh(second);
    REQUIRE(combined.GetSubmeshes().size() == 2);
    CHECK(combined.submeshes[0] == Spot::SubMesh{0, static_cast<std::uint32_t>(indexCount)});
    CHECK(combined.submeshes[1] ==
          Spot::SubMesh{static_cast<std::uint32_t>(indexCount), static_cast<std::uint32_t>(second.indices.size())});
    for (std::size_t i = 0; i < second.indices.size(); ++i) {
        CHECK(combined.indices[indexCount + i] == vertexCount + second.indices[i]);
    }
}

TEST_CASE("Mesh data and its bounds work without a graphics device", "[graphics][geometry]") {
    Spot::MeshData mesh;
    for (const float x : {-2.0f, 2.0f}) {
        for (const float y : {-2.0f, 2.0f}) {
            for (const float z : {-2.0f, 2.0f}) {
                mesh.vertices.push_back({{x, y, z}, {0, 1, 0}, {0, 0}});
            }
        }
    }
    const auto box = mesh.GetBoundingBox();
    CHECK((box.GetCenter() == Spot::Vector3{0, 0, 0}));
    CHECK((box.GetSize() == Spot::Vector3{4, 4, 4}));
    const auto sphere = mesh.GetBounds();
    CHECK((sphere.center == Spot::Vector3{0, 0, 0}));
    for (const auto& vertex : mesh.vertices) {
        CHECK(Spot::Length(vertex.position - sphere.center) <= sphere.radius);
    }
}
