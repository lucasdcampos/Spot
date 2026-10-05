#pragma once

#include "graphics/material.h"
#include "graphics/mesh.h"
#include "graphics/model_data.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Spot {

class GraphicsDevice;

struct ModelPart {
    std::string name;
    std::unique_ptr<Mesh> mesh;
    // Index into Model::GetMaterials().
    std::size_t material = 0;
};

// Meshes with their materials and textures on the GPU, ready for Renderer3D::DrawModel().
class Model {
public:
    // Returns null, after logging why, when the file cannot be loaded (see ModelData::Load()) or the GPU resources
    // cannot be created.
    static std::unique_ptr<Model> Load(GraphicsDevice& device, const std::filesystem::path& path);
    static std::unique_ptr<Model> Create(GraphicsDevice& device, const ModelData& data);

    ~Model();

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    std::span<const ModelPart> GetParts() const { return m_parts; }
    // Materials can be changed, such as to tint the model; parts without a material in the file share a white one.
    std::span<Material> GetMaterials() { return m_materials; }
    std::span<const Material> GetMaterials() const { return m_materials; }

private:
    Model() = default;

    std::vector<ModelPart> m_parts;
    std::vector<Material> m_materials;
};

} // namespace Spot
