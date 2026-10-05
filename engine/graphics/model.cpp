#include "graphics/model.h"

#include "core/log.h"
#include "core/paths.h"
#include "graphics/graphics_device.h"

#include <optional>

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
    std::vector<AssetRef<Texture>> textures;
    for (const auto& image : data.images) {
        AssetRef<Texture> texture = device.CreateTexture(image, true);
        if (!texture) {
            return nullptr;
        }
        textures.push_back(std::move(texture));
    }

    std::vector<AssetRef<Material>> materials;
    for (const auto& source : data.materials) {
        Material material{.baseColor = source.baseColor, .baseColorTexture = {}, .doubleSided = source.doubleSided};
        if (source.baseColorImage) {
            material.baseColorTexture = textures[*source.baseColorImage];
        }
        materials.emplace_back(std::make_shared<const Material>(std::move(material)));
    }

    // Parts without a material share a plain white one.
    const AssetRef<Material> defaultMaterial = std::make_shared<const Material>();
    std::vector<ModelPart> parts;
    for (const auto& source : data.meshes) {
        // Parts can be empty, such as when every index was out of range; they have nothing to draw.
        if (source.mesh.indices.empty()) {
            continue;
        }
        AssetRef<Mesh> mesh = Mesh::Create(device, source.mesh);
        if (!mesh) {
            return nullptr;
        }
        parts.push_back(
            {source.name, std::move(mesh), source.material ? materials[*source.material] : defaultMaterial});
    }
    return std::make_unique<Model>(std::move(parts));
}

} // namespace Spot
