#pragma once

#include "spot/core/ids/asset_ref.h"
#include "spot/render/material.h"
#include "spot/render/mesh.h"
#include "spot/render/model_data.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Spot {

class GraphicsDevice;

// A node of a model's hierarchy: where it is relative to its parent, and the mesh it shows, with a material per
// submesh.
struct ModelNode {
    std::string name;
    Matrix4 transform = {};
    // An index into the model's nodes, which lists parents before their children; none for root nodes.
    std::optional<std::size_t> parent = std::nullopt;
    AssetRef<Mesh> mesh = {};
    // Per submesh of the mesh; null ones draw plain white.
    std::vector<AssetRef<Material>> materials = {};
    AssetRef<Skeleton> skeleton = {};
    std::vector<AssetRef<AnimationClip>> animations = {};
    bool joint = false;
};

// A submesh of a node's mesh with its material, placed in the model: what Renderer3D::DrawModel() draws.
struct ModelPart {
    std::string name;
    AssetRef<Mesh> mesh;
    AssetRef<Material> material;
    // Where it is in the model: its node's transform, through the node's parents.
    Matrix4 transform = {};
    std::uint32_t submesh = 0;
    // The index of its node.
    std::size_t node = 0;
};

// A hierarchy of nodes showing meshes with their materials, ready for Renderer3D::DrawModel(). Models are usually
// assets, loaded through AssetManager; Load() and Create() make them straight from a file or from CPU data instead.
class Model {
public:
    // Returns null, after logging why, when the file cannot be loaded (see ModelData::Load()) or the GPU resources
    // cannot be created.
    static std::unique_ptr<Model> Load(GraphicsDevice& device, const std::filesystem::path& path);
    static std::unique_ptr<Model> Create(GraphicsDevice& device, const ModelData& data);

    explicit Model(std::vector<ModelNode> nodes);

    std::span<const ModelNode> GetNodes() const { return m_nodes; }
    // Where the node is in the model, through its parents.
    Matrix4 GetNodeTransform(std::size_t node) const;

    // Every submesh of every node, flattened, as drawing them needs.
    std::span<const ModelPart> GetParts() const { return m_parts; }
    // Parts can be changed, such as to give one another material.
    std::span<ModelPart> GetParts() { return m_parts; }

private:
    std::vector<ModelNode> m_nodes;
    std::vector<ModelPart> m_parts;
};

} // namespace Spot
