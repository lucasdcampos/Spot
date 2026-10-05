#pragma once

#include "graphics/color.h"
#include "graphics/image.h"
#include "graphics/mesh_data.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Spot {

// A surface's look as a model file describes it, before its images are on the GPU.
struct MaterialData {
    std::string name;
    Color baseColor = Color::White;
    // Index into ModelData::images; its colors are multiplied by baseColor.
    std::optional<std::size_t> baseColorImage = std::nullopt;
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
    // triangles are read, with their positions, normals and first texture coordinates, and only the base color of
    // materials. Returns nothing, after logging why, when the file cannot be read.
    static std::optional<ModelData> Load(const std::filesystem::path& path);
    // Reads a .glb, or a .gltf whose buffers and images are embedded as data URIs.
    static std::optional<ModelData> LoadFromMemory(std::span<const std::byte> data);
};

} // namespace Spot
