#include "assets/asset_manager.h"

#include "assets/asset_file.h"
#include "assets/asset_formats.h"
#include "assets/asset_manifest.h"
#include "assets/built_in_assets.h"
#include "audio/sound.h"
#include "core/log.h"
#include "core/paths.h"
#include "graphics/font.h"
#include "graphics/graphics_device.h"
#include "graphics/image.h"
#include "graphics/material.h"
#include "graphics/mesh.h"
#include "graphics/model.h"
#include "scene/scene.h"

#include <array>
#include <utility>
#include <vector>

namespace Spot {

namespace {

struct BuiltIn {
    AssetId id;
    AssetType type;
    const char* path;
};

constexpr std::array BuiltIns{
    BuiltIn{BuiltInAssets::CubeMesh, AssetType::Mesh, "builtin/cube"},
    BuiltIn{BuiltInAssets::SphereMesh, AssetType::Mesh, "builtin/sphere"},
    BuiltIn{BuiltInAssets::PlaneMesh, AssetType::Mesh, "builtin/plane"},
    BuiltIn{BuiltInAssets::GridTexture, AssetType::Texture, "builtin/grid"},
    BuiltIn{BuiltInAssets::CheckerTexture, AssetType::Texture, "builtin/checker"},
};

} // namespace

AssetManager::AssetManager(std::filesystem::path importDirectory, GraphicsDevice* device)
    : m_importDirectory(std::move(importDirectory)), m_device(device) {
    RefreshManifest();
}

AssetManager::~AssetManager() = default;

void AssetManager::RefreshManifest() {
    const auto manifest = Detail::Manifest::LoadFromFile(Detail::Manifest::GetPath(m_importDirectory));
    if (!manifest) {
        Log::GetEngineLogger().Warning("No asset manifest in '{}'; no assets can be loaded",
                                       PathToUtf8(m_importDirectory));
    }

    std::unordered_map<AssetId, Entry> entries;
    m_paths.clear();
    for (const auto& entry : manifest ? manifest->entries : std::vector<Detail::ManifestEntry>()) {
        // Assets keep what they already loaded, so references to them stay shared.
        const auto previous = m_entries.find(entry.id);
        entries[entry.id] = {entry.type, entry.path,
                             previous != m_entries.end() ? previous->second.loaded : std::weak_ptr<const void>()};
        // The first of sub-assets with the same name keeps the path.
        m_paths.try_emplace(entry.parent.IsValid() ? entry.path + "#" + entry.name : entry.path, entry.id);
    }

    for (const auto& [id, type, path] : BuiltIns) {
        const auto previous = m_entries.find(id);
        entries[id] = {type, path, previous != m_entries.end() ? previous->second.loaded : std::weak_ptr<const void>()};
        m_paths[path] = id;
    }
    m_entries = std::move(entries);
}

std::optional<AssetType> AssetManager::GetType(AssetId id) const {
    const auto found = m_entries.find(id);
    return found != m_entries.end() ? std::optional(found->second.type) : std::nullopt;
}

std::optional<AssetId> AssetManager::FindByPath(std::string_view path) const {
    const auto found = m_paths.find(std::string(path));
    return found != m_paths.end() ? std::optional(found->second) : std::nullopt;
}

std::unique_ptr<Scene> AssetManager::InstantiateScene(AssetId id) {
    if (GetType(id) != AssetType::Scene) {
        Log::GetEngineLogger().Error("Asset {} is not a scene", id.ToString());
        return nullptr;
    }
    const auto data = Detail::ReadAssetFile(Detail::GetImportedAssetPath(m_importDirectory, id), AssetType::Scene);
    const auto text = data ? Detail::DecodeScene(*data) : std::nullopt;
    auto scene = std::make_unique<Scene>();
    if (!text || !scene->LoadFromString(*text, this)) {
        Log::GetEngineLogger().Error("Could not instantiate scene {} from '{}'", id.ToString(), m_entries.at(id).path);
        return nullptr;
    }
    return scene;
}

std::unique_ptr<Scene> AssetManager::InstantiateScene(std::string_view path) {
    const auto id = FindByPath(path);
    if (!id) {
        LogUnknownPath(path);
        return nullptr;
    }
    return InstantiateScene(*id);
}

void AssetManager::LogUnknownPath(std::string_view path) {
    Log::GetEngineLogger().Error("No asset at '{}'", path);
}

std::shared_ptr<const void> AssetManager::LoadAsset(AssetId id, AssetType type) {
    if (!id.IsValid()) {
        return nullptr;
    }
    const auto found = m_entries.find(id);
    if (found == m_entries.end()) {
        Log::GetEngineLogger().Error("No asset has the id {}", id.ToString());
        return nullptr;
    }
    if (found->second.type != type) {
        Log::GetEngineLogger().Error("Asset {} ('{}') is a {}, not a {}", id.ToString(), found->second.path,
                                     GetAssetTypeName(found->second.type), GetAssetTypeName(type));
        return nullptr;
    }
    if (auto loaded = found->second.loaded.lock()) {
        return loaded;
    }

    auto asset = Create(id, type);
    if (!asset) {
        Log::GetEngineLogger().Error("Could not load {} {} from '{}'", GetAssetTypeName(type), id.ToString(),
                                     found->second.path);
        return nullptr;
    }
    // Looked up again: loading the asset's own references may have added entries to the map.
    m_entries.at(id).loaded = asset;
    return asset;
}

std::shared_ptr<const void> AssetManager::Create(AssetId id, AssetType type) {
    if (id == BuiltInAssets::CubeMesh || id == BuiltInAssets::SphereMesh || id == BuiltInAssets::PlaneMesh) {
        if (!m_device) {
            Log::GetEngineLogger().Error("Mesh assets need a graphics device");
            return nullptr;
        }
        const MeshData mesh = id == BuiltInAssets::CubeMesh     ? MeshData::CreateCube()
                              : id == BuiltInAssets::SphereMesh ? MeshData::CreateSphere()
                                                                : MeshData::CreatePlane();
        std::shared_ptr<const Mesh> result = Mesh::Create(*m_device, mesh);
        return result;
    }
    if (id == BuiltInAssets::GridTexture || id == BuiltInAssets::CheckerTexture) {
        if (!m_device) {
            Log::GetEngineLogger().Error("Texture assets need a graphics device");
            return nullptr;
        }
        const Image image = id == BuiltInAssets::GridTexture ? Image::CreateGrid() : Image::CreateChecker();
        std::shared_ptr<Texture> result = m_device->CreateTexture(image, true);
        return result;
    }

    const auto data = Detail::ReadAssetFile(Detail::GetImportedAssetPath(m_importDirectory, id), type);
    if (!data) {
        return nullptr;
    }

    const bool needsDevice = type == AssetType::Texture || type == AssetType::Mesh || type == AssetType::Font;
    if (needsDevice && !m_device) {
        Log::GetEngineLogger().Error("{} assets need a graphics device", GetAssetTypeName(type));
        return nullptr;
    }

    switch (type) {
    case AssetType::Texture: {
        const auto texture = Detail::DecodeTexture(*data);
        if (!texture) {
            break;
        }
        std::shared_ptr<Texture> result = m_device->CreateTexture(texture->image, texture->mipmaps);
        return result;
    }
    case AssetType::Mesh: {
        const auto mesh = Detail::DecodeMesh(*data);
        if (!mesh) {
            break;
        }
        std::shared_ptr<const Mesh> result = Mesh::Create(*m_device, *mesh);
        return result;
    }
    case AssetType::Material: {
        const auto material = Detail::DecodeMaterial(*data);
        if (!material) {
            break;
        }
        return std::make_shared<const Material>(Material{.baseColor = material->baseColor,
                                                         .baseColorTexture = Load<Texture>(material->baseColorTexture),
                                                         .doubleSided = material->doubleSided,
                                                         .worldTextureSize = material->worldTextureSize});
    }
    case AssetType::Model: {
        const auto parts = Detail::DecodeModel(*data);
        if (!parts) {
            break;
        }
        std::vector<ModelPart> loaded;
        for (const auto& part : *parts) {
            loaded.push_back({part.name, Load<Mesh>(part.mesh), Load<Material>(part.material)});
        }
        return std::make_shared<const Model>(std::move(loaded));
    }
    case AssetType::Sound: {
        const auto sound = Detail::DecodeSound(*data);
        if (!sound) {
            break;
        }
        std::vector<float> samples(sound->samples.size());
        for (std::size_t i = 0; i < samples.size(); ++i) {
            samples[i] = static_cast<float>(sound->samples[i]) / 32768.0f;
        }
        return std::make_shared<const Sound>(sound->sampleRate, sound->channelCount, std::move(samples));
    }
    case AssetType::Font: {
        const auto font = Detail::DecodeFont(*data);
        if (!font) {
            break;
        }
        std::shared_ptr<const Font> result = Font::LoadFromMemory(*m_device, font->fontData, font->pixelSize);
        return result;
    }
    case AssetType::Scene:
        Log::GetEngineLogger().Error("Scenes are not loaded as shared assets");
        return nullptr;
    }

    Log::GetEngineLogger().Error("Imported asset {} is corrupt; import it again", id.ToString());
    return nullptr;
}

} // namespace Spot
