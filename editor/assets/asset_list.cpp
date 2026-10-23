#include "assets/asset_list.h"

#include "imgui/icons.h"
#include "project/editor_project.h"

#include <algorithm>
#include <array>

namespace Spot::Editor {

namespace {

const std::array BuiltIns{
    AssetEntry{BuiltInAssets::CubeMesh, AssetType::Mesh, "Cube", "builtin/cube", true},
    AssetEntry{BuiltInAssets::SphereMesh, AssetType::Mesh, "Sphere", "builtin/sphere", true},
    AssetEntry{BuiltInAssets::PlaneMesh, AssetType::Mesh, "Plane", "builtin/plane", true},
    AssetEntry{BuiltInAssets::GridTexture, AssetType::Texture, "Grid", "builtin/grid", true},
    AssetEntry{BuiltInAssets::CheckerTexture, AssetType::Texture, "Checker", "builtin/checker", true},
};

AssetEntry MakeEntry(const AssetInfo& info) {
    std::string name = info.name;
    if (name.empty()) {
        const std::size_t slash = info.path.find_last_of('/');
        name = slash == std::string::npos ? info.path : info.path.substr(slash + 1);
    }
    return {info.id, info.type, std::move(name), info.path, false};
}

} // namespace

std::vector<AssetEntry> ListAssets(EditorProject* project, std::optional<AssetType> type) {
    std::vector<AssetEntry> entries;
    for (const AssetEntry& entry : BuiltIns) {
        if (!type || entry.type == *type) {
            entries.push_back(entry);
        }
    }
    if (project) {
        for (const AssetInfo& info : project->GetAssetDatabase().GetAssets()) {
            if (!type || info.type == *type) {
                entries.push_back(MakeEntry(info));
            }
        }
    }
    return entries;
}

std::optional<AssetEntry> FindAsset(EditorProject* project, AssetId id) {
    if (!id.IsValid()) {
        return std::nullopt;
    }
    const auto builtIn = std::ranges::find(BuiltIns, id, &AssetEntry::id);
    if (builtIn != BuiltIns.end()) {
        return *builtIn;
    }
    if (project) {
        if (const AssetInfo* info = project->GetAssetDatabase().Find(id)) {
            return MakeEntry(*info);
        }
    }
    return std::nullopt;
}

const char* GetAssetTypeIcon(AssetType type) {
    switch (type) {
    case AssetType::Script:
        return ICON_FA_FILE_CODE;
    case AssetType::SpriteAtlas:
    case AssetType::Texture:
        return ICON_FA_IMAGE;
    case AssetType::Mesh:
        return ICON_FA_DRAW_POLYGON;
    case AssetType::Material:
        return ICON_FA_PALETTE;
    case AssetType::Model:
        return ICON_FA_CUBES;
    case AssetType::Sound:
        return ICON_FA_VOLUME_HIGH;
    case AssetType::Font:
        return ICON_FA_FONT;
    case AssetType::Scene:
        return ICON_FA_MOUNTAIN_SUN;
    case AssetType::AnimatorController:
    case AssetType::Skeleton:
    case AssetType::AnimationClip:
    case AssetType::Prefab:
        return ICON_FA_CUBE;
    }
    return ICON_FA_FILE;
}

} // namespace Spot::Editor
