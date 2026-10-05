#pragma once

#include "assets/asset_ref.h"
#include "graphics/material.h"
#include "graphics/mesh.h"
#include "graphics/model_data.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Spot {

class GraphicsDevice;

struct ModelPart {
    std::string name;
    AssetRef<Mesh> mesh;
    AssetRef<Material> material;
};

// Meshes with their materials, ready for Renderer3D::DrawModel(). Models are usually assets, loaded through
// AssetManager; Load() and Create() make them straight from a file or from CPU data instead.
class Model {
public:
    // Returns null, after logging why, when the file cannot be loaded (see ModelData::Load()) or the GPU resources
    // cannot be created.
    static std::unique_ptr<Model> Load(GraphicsDevice& device, const std::filesystem::path& path);
    static std::unique_ptr<Model> Create(GraphicsDevice& device, const ModelData& data);

    explicit Model(std::vector<ModelPart> parts) : m_parts(std::move(parts)) {}

    std::span<const ModelPart> GetParts() const { return m_parts; }
    // Parts can be changed, such as to give one another material.
    std::span<ModelPart> GetParts() { return m_parts; }

private:
    std::vector<ModelPart> m_parts;
};

} // namespace Spot
