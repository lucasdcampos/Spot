#include "graphics/model.h"

#include "core/log.h"
#include "core/paths.h"
#include "graphics/graphics_device.h"

#include <optional>
#include <utility>

namespace Spot {

std::unique_ptr<Model> Model::Load(GraphicsDevice& device, const std::filesystem::path& path) {
    const auto data = ModelData::Load(path);
    if (!data) {
        return nullptr;
    }
    auto model = Create(device, *data);
    if (!model) {
        Log::GetEngineLogger().Error("Could not create the GPU resources of model '{}'", PathToUtf8(path));
    }
    return model;
}

std::unique_ptr<Model> Model::Create(GraphicsDevice& device, const ModelData& data) {
    std::vector<std::shared_ptr<const Texture>> textures;
    for (const auto& image : data.images) {
        std::shared_ptr<const Texture> texture = device.CreateTexture(image, true);
        if (!texture) {
            return nullptr;
        }
        textures.push_back(std::move(texture));
    }

    auto model = std::unique_ptr<Model>(new Model());
    for (const auto& source : data.materials) {
        Material material{.baseColor = source.baseColor, .doubleSided = source.doubleSided};
        if (source.baseColorImage) {
            material.baseColorTexture = textures[*source.baseColorImage];
        }
        model->m_materials.push_back(std::move(material));
    }

    std::optional<std::size_t> defaultMaterial;
    for (const auto& source : data.meshes) {
        // Parts can be empty, such as when every index was out of range; they have nothing to draw.
        if (source.mesh.indices.empty()) {
            continue;
        }
        auto mesh = Mesh::Create(device, source.mesh);
        if (!mesh) {
            return nullptr;
        }

        if (!source.material && !defaultMaterial) {
            defaultMaterial = model->m_materials.size();
            model->m_materials.emplace_back();
        }
        model->m_parts.push_back({source.name, std::move(mesh), source.material ? *source.material : *defaultMaterial});
    }
    return model;
}

Model::~Model() = default;

} // namespace Spot
