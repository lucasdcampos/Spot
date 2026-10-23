#pragma once

#include "spot/animation/animation_data.h"
#include "spot/core/color.h"
#include "spot/core/geometry/mesh_data.h"
#include "spot/core/matrix.h"
#include "spot/graphics/image.h"
#include "spot/render/material.h"

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

// A mesh of the model, in its own space, which nodes place; its glTF primitives are its submeshes.
struct ModelMeshData {
    std::string name;
    MeshData mesh;
    // Per submesh: an index into ModelData::materials, or none for the default material.
    std::vector<std::optional<std::size_t>> materials;
};

// A node of the model's hierarchy, as the file names and places it.
struct ModelNodeData {
    std::string name;
    // Relative to the parent node, or to the model for root nodes.
    Matrix4 transform;
    // Index into ModelData::meshes, for nodes that show one.
    std::optional<std::size_t> mesh = std::nullopt;
    // Index into ModelData::nodes, which lists parents before their children; none for root nodes.
    std::optional<std::size_t> parent = std::nullopt;
    std::optional<std::size_t> skin = std::nullopt;
    bool joint = false;
};

// A model read from a file into CPU memory. Model::Create() sends it to the GPU.
struct ModelData {
    std::vector<Image> images;
    std::vector<MaterialData> materials;
    std::vector<ModelMeshData> meshes;
    std::vector<ModelNodeData> nodes;
    std::vector<Skeleton> skeletons;
    std::vector<AnimationClip> animations;

    // Reads glTF 2.0, as .gltf with its files next to it or as a single .glb: the nodes of the file's default scene,
    // with their names and transforms, the meshes they show, each with its primitives as submeshes, and the materials
    // they use. Only triangles are read, with their positions, normals, tangents and first texture coordinates, and
    // the metallic-roughness inputs of materials. Returns nothing, after logging why, when the file cannot be read.
    static std::optional<ModelData> Load(const std::filesystem::path& path);
    // Reads a .glb, or a .gltf whose buffers and images are embedded as data URIs.
    static std::optional<ModelData> LoadFromMemory(std::span<const std::byte> data);

    // Whether an image holds colors, which are sRGB, rather than data such as normals, which is read as it is: only
    // images that materials use solely for data are not colors.
    bool IsColorImage(std::size_t image) const;
    // Where the node is in the model, through its parents.
    Matrix4 GetWorldTransform(std::size_t node) const;
};

} // namespace Spot
