#pragma once

#include "graphics/color.h"
#include "graphics/image.h"
#include "graphics/material.h"
#include "graphics/mesh_data.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Spot {

// A surface's look as a model file describes it, before its images are on the GPU: a Material, with its textures
// given as indices into ModelData::images. The defaults are glTF's.
struct MaterialData {
    std::string name;
    Color baseColor = Color::White;
    std::optional<std::size_t> baseColorImage = std::nullopt;
    float metallic = 1.0f;
    float roughness = 1.0f;
    std::optional<std::size_t> metallicRoughnessImage = std::nullopt;
    std::optional<std::size_t> normalImage = std::nullopt;
    float normalScale = 1.0f;
    std::optional<std::size_t> occlusionImage = std::nullopt;
    float occlusionStrength = 1.0f;
    // glTF's emissive factor, times the strength KHR_materials_emissive_strength gives.
    Color emissive = Color::Black;
    std::optional<std::size_t> emissiveImage = std::nullopt;
    // glTF's alphaMode: OPAQUE, MASK (cutout) or BLEND (transparent).
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    // Drawn from both sides instead of hiding back faces, for thin surfaces such as leaves or cloth.
    bool doubleSided = false;
};

struct ModelMeshData {
    std::string name;
    MeshData mesh;
    // Index into ModelData::materials, or none for the default material.
    std::optional<std::size_t> material = std::nullopt;
};

// A model read from a file into CPU memory. Model::Create() sends it to the GPU.
struct ModelData {
    std::vector<Image> images;
    std::vector<MaterialData> materials;
    std::vector<ModelMeshData> meshes;

    // Reads glTF 2.0, as .gltf with its files next to it or as a single .glb. Meshes are placed where the file's
    // default scene puts them: node transforms are applied to the vertices, so the model has no hierarchy. Only
    // triangles are read, with their positions, normals, tangents and first texture coordinates, and the
    // metallic-roughness inputs of materials. Returns nothing, after logging why, when the file cannot be read.
    static std::optional<ModelData> Load(const std::filesystem::path& path);
    // Reads a .glb, or a .gltf whose buffers and images are embedded as data URIs.
    static std::optional<ModelData> LoadFromMemory(std::span<const std::byte> data);

    // Whether an image holds colors, which are sRGB, rather than data such as normals, which is read as it is: only
    // images that materials use solely for data are not colors.
    bool IsColorImage(std::size_t image) const;
};

} // namespace Spot
