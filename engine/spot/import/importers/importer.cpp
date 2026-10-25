#include "spot/import/importers/importer.h"

#include <utility>

namespace Spot::Detail {

bool ImportContext::WriteAsset(AssetType type, const BinaryWriter& data) {
    if (!Write(m_meta.id, type, data)) {
        return false;
    }
    m_imported.push_back({m_meta.id, type, {}, {}});
    return true;
}

AssetId ImportContext::WriteSubAsset(std::string_view key, AssetType type, std::string name, const BinaryWriter& data) {
    const AssetId id = m_meta.GetOrAddSubAssetId(key);
    if (!Write(id, type, data)) {
        return {};
    }
    m_imported.push_back({id, type, std::move(name), m_meta.id});
    return id;
}

bool ImportContext::Write(AssetId id, AssetType type, const BinaryWriter& data) {
    return WriteAssetFile(GetImportedAssetPath(m_importDirectory, id), type, data.GetData());
}

ImporterInfo FindImporter(AssetType type) {
    switch (type) {
    case AssetType::Texture:
        return {&ImportTexture, 4};
    case AssetType::SpriteAtlas:
        return {&ImportSpriteAtlas, 1};
    case AssetType::Model:
        return {&ImportModel, 14};
    case AssetType::AnimatorController:
        return {&ImportAnimatorController, 2};
    case AssetType::Material:
        return {&ImportMaterial, 7};
    case AssetType::Scene:
        return {&ImportScene, 2};
    case AssetType::Script:
        return {&ImportScript, 1};
    case AssetType::Prefab:
        return {&ImportPrefab, 4};
    case AssetType::Sound:
        return {&ImportSound, 2};
    case AssetType::Font:
        return {&ImportFont, 2};
    default:
        return {};
    }
}

} // namespace Spot::Detail
