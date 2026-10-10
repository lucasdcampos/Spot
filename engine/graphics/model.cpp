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
                     .alphaMode = source.alphaMode,
                     .alphaCutoff = source.alphaCutoff,
                     .doubleSided = source.doubleSided}));
    }

    std::vector<AssetRef<Mesh>> meshes;
    for (const auto& source : data.meshes) {
        AssetRef<Mesh> mesh = Mesh::Create(device, source.mesh);
        if (!mesh) {
            return nullptr;
        }
        meshes.push_back(std::move(mesh));
    }

    // Submeshes without a material share a plain white one.
    const AssetRef<Material> defaultMaterial = std::make_shared<const Material>();
    std::vector<ModelNode> nodes;
    for (const auto& source : data.nodes) {
        ModelNode node{.name = source.name, .transform = source.transform, .parent = source.parent};
        if (source.mesh) {
            node.mesh = meshes[*source.mesh];
            for (const auto& material : data.meshes[*source.mesh].materials) {
                node.materials.push_back(material ? materials[*material] : defaultMaterial);
            }
        }
        nodes.push_back(std::move(node));
    }
    return std::make_unique<Model>(std::move(nodes));
}

Model::Model(std::vector<ModelNode> nodes) : m_nodes(std::move(nodes)) {
    for (std::size_t i = 0; i < m_nodes.size(); ++i) {
        const ModelNode& node = m_nodes[i];
        // Meshes not loaded, as without a GPU, still have their parts, as many as their materials.
        if (!node.mesh && !node.mesh.GetId().IsValid()) {
            continue;
        }
        const Matrix4 transform = GetNodeTransform(i);
        const auto submeshes =
            static_cast<std::uint32_t>(node.mesh ? node.mesh->GetSubmeshes().size() : node.materials.size());
        for (std::uint32_t submesh = 0; submesh < submeshes; ++submesh) {
            m_parts.push_back(
                {.name = node.name,
                 .mesh = node.mesh,
                 .material = submesh < node.materials.size() ? node.materials[submesh] : AssetRef<Material>(),
                 .transform = transform,
                 .submesh = submesh,
                 .node = i});
        }
    }
}

Matrix4 Model::GetNodeTransform(std::size_t node) const {
    Matrix4 transform = m_nodes[node].transform;
    for (std::optional<std::size_t> parent = m_nodes[node].parent; parent; parent = m_nodes[*parent].parent) {
        transform = m_nodes[*parent].transform * transform;
    }
    return transform;
}

} // namespace Spot
