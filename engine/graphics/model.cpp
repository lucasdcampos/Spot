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
    for (std::size_t i = 0; i < data.images.size(); ++i) {
        AssetRef<Texture> texture = device.CreateTexture(
            data.images[i], true, data.IsColorImage(i) ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8);
        if (!texture) {
            return nullptr;
        }
        textures.push_back(std::move(texture));
    }

    const auto texture = [&](std::optional<std::size_t> image) {
        return image ? textures[*image] : AssetRef<Texture>();
    };
    std::vector<AssetRef<Material>> materials;
    for (const auto& source : data.materials) {
        materials.emplace_back(std::make_shared<const Material>(
            Material{.baseColor = source.baseColor,
                     .baseColorTexture = texture(source.baseColorImage),
                     .metallic = source.metallic,
                     .roughness = source.roughness,
                     .metallicRoughnessTexture = texture(source.metallicRoughnessImage),
                     .normalTexture = texture(source.normalImage),
                     .normalScale = source.normalScale,
                     .occlusionTexture = texture(source.occlusionImage),
                     .occlusionStrength = source.occlusionStrength,
                     .emissive = source.emissive,
                     .emissiveTexture = texture(source.emissiveImage),
                     .doubleSided = source.doubleSided}));
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
