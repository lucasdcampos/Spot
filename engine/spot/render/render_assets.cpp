#include "spot/render/render_assets.h"
#include "spot/assets/asset_formats.h"
#include "spot/assets/asset_manager.h"
#include "spot/assets/built_in_assets.h"
#include "spot/core/log.h"
#include "spot/graphics/graphics_device.h"
#include "spot/render/font.h"
#include "spot/render/material.h"
#include "spot/render/mesh.h"
#include "spot/render/model.h"
#include "spot/render/sprite_atlas.h"

#include <algorithm>
#include <utility>

namespace Spot {
namespace {
std::vector<AssetId> MaterialReferences(const MaterialSource& material) {
    return {material.baseColorTexture, material.metallicRoughnessTexture, material.normalTexture,
            material.occlusionTexture, material.emissiveTexture};
}
std::vector<AssetId> ModelReferences(const std::vector<Detail::ModelFileNode>& nodes) {
    std::vector<AssetId> ids;
    for (const auto& node : nodes) {
        ids.push_back(node.mesh);
        ids.push_back(node.skeleton);
        ids.insert(ids.end(), node.materials.begin(), node.materials.end());
        ids.insert(ids.end(), node.animations.begin(), node.animations.end());
    }
    return ids;
}
} // namespace
void RegisterRenderAssets(AssetManager& assets, GraphicsDevice* device) {
    assets.RegisterLoader(AssetType::Mesh,
                          MakeAssetLoader<MeshData>(
                              [](AssetId id, std::span<const std::byte> data) -> std::optional<MeshData> {
                                  if (id == BuiltInAssets::CubeMesh)
                                      return MeshData::CreateCube();
                                  if (id == BuiltInAssets::SphereMesh)
                                      return MeshData::CreateSphere();
                                  if (id == BuiltInAssets::PlaneMesh)
                                      return MeshData::CreatePlane();
                                  return Detail::DecodeMesh(data);
                              },
                              [device](AssetManager&, AssetId, MeshData& mesh) -> std::shared_ptr<const void> {
                                  if (!device) {
                                      Log::GetEngineLogger().Error("Mesh assets need a graphics device");
                                      return nullptr;
                                  }
                                  return std::shared_ptr<const Mesh>(Mesh::Create(*device, mesh));
                              }));
    auto materialLoader = MakeAssetLoader<MaterialSource>(
        [](AssetId, std::span<const std::byte> data) { return Detail::DecodeMaterial(data); },
        [](AssetManager& assets, AssetId, MaterialSource& material) -> std::shared_ptr<const void> {
            return std::make_shared<const Material>(
                Material{.baseColor = material.baseColor,
                         .baseColorTexture = assets.Load<Texture>(material.baseColorTexture),
                         .metallic = material.metallic,
                         .roughness = material.roughness,
                         .metallicRoughnessTexture = assets.Load<Texture>(material.metallicRoughnessTexture),
                         .normalTexture = assets.Load<Texture>(material.normalTexture),
                         .normalScale = material.normalScale,
                         .occlusionTexture = assets.Load<Texture>(material.occlusionTexture),
                         .occlusionStrength = material.occlusionStrength,
                         .emissive = material.emissive,
                         .emissiveTexture = assets.Load<Texture>(material.emissiveTexture),
                         .alphaMode = material.alphaMode,
                         .alphaCutoff = material.alphaCutoff,
                         .doubleSided = material.doubleSided,
                         .worldTextureSize = material.worldTextureSize,
                         .nearestFiltering = material.nearestFiltering});
        });
    materialLoader.references = [](const void* data) {
        return MaterialReferences(*static_cast<const MaterialSource*>(data));
    };
    materialLoader.dependencies = [](const void* data) {
        const auto& material = *static_cast<const Material*>(data);
        return std::vector<AssetId>{material.baseColorTexture.GetId(), material.metallicRoughnessTexture.GetId(),
                                    material.normalTexture.GetId(), material.occlusionTexture.GetId(),
                                    material.emissiveTexture.GetId()};
    };
    assets.RegisterLoader(AssetType::Material, std::move(materialLoader));
    auto modelLoader = MakeAssetLoader<std::vector<Detail::ModelFileNode>>(
        [](AssetId, std::span<const std::byte> data) { return Detail::DecodeModel(data); },
        [](AssetManager& assets, AssetId, std::vector<Detail::ModelFileNode>& nodes) -> std::shared_ptr<const void> {
            std::vector<ModelNode> loaded;
            for (const auto& node : nodes) {
                ModelNode& added = loaded.emplace_back(ModelNode{
                    .name = node.name,
                    .transform = node.transform,
                    .parent = node.parent >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(node.parent))
                                               : std::nullopt});
                added.joint = node.joint;
                added.skeleton = assets.Load<Skeleton>(node.skeleton);
                for (const auto clip : node.animations) {
                    added.animations.push_back(assets.Load<AnimationClip>(clip));
                }
                if (node.mesh.IsValid()) {
                    added.mesh = assets.Load<Mesh>(node.mesh);
                }
                for (const AssetId material : node.materials) {
                    added.materials.push_back(assets.Load<Material>(material));
                }
            }
            return std::make_shared<const Model>(std::move(loaded));
        });
    modelLoader.references = [](const void* data) {
        return ModelReferences(*static_cast<const std::vector<Detail::ModelFileNode>*>(data));
    };
    modelLoader.dependencies = [](const void* data) {
        std::vector<AssetId> ids;
        const auto& model = *static_cast<const Model*>(data);
        for (const auto& part : model.GetParts()) {
            ids.push_back(part.mesh.GetId());
            ids.push_back(part.material.GetId());
        }
        for (const auto& node : model.GetNodes()) {
            ids.push_back(node.skeleton.GetId());
            for (const auto& clip : node.animations)
                ids.push_back(clip.GetId());
        }
        return ids;
    };
    assets.RegisterLoader(AssetType::Model, std::move(modelLoader));
    assets.RegisterLoader(AssetType::Font,
                          MakeAssetLoader<FontAtlas>(
                              [](AssetId, std::span<const std::byte> data) -> std::optional<FontAtlas> {
                                  const auto font = Detail::DecodeFont(data);
                                  return font ? FontAtlas::Create(font->fontData, font->pixelSize) : std::nullopt;
                              },
                              [device](AssetManager&, AssetId, FontAtlas& atlas) -> std::shared_ptr<const void> {
                                  if (!device) {
                                      Log::GetEngineLogger().Error("Font assets need a graphics device");
                                      return nullptr;
                                  }
                                  return std::shared_ptr<const Font>(Font::Create(*device, std::move(atlas)));
                              }));
    auto atlasLoader = MakeAssetLoader<SpriteAtlas>(
        [](AssetId, std::span<const std::byte> data) -> std::optional<SpriteAtlas> {
            const auto text = Detail::DecodeScene(data);
            return text ? SpriteAtlas::LoadFromString(*text) : std::nullopt;
        },
        [](AssetManager& assets, AssetId, SpriteAtlas& atlas) -> std::shared_ptr<const void> {
            atlas.texture = assets.Load<Texture>(atlas.texture.GetId());
            return atlas.IsValid() ? std::make_shared<const SpriteAtlas>(std::move(atlas)) : nullptr;
        });
    atlasLoader.textReferences = true;
    atlasLoader.dependencies = [](const void* data) {
        return std::vector<AssetId>{static_cast<const SpriteAtlas*>(data)->texture.GetId()};
    };
    assets.RegisterLoader(AssetType::SpriteAtlas, std::move(atlasLoader));
}
} // namespace Spot
