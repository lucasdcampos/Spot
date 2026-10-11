#include "world_save.h"

#include "physics_gun.h"
#include "projectile.h"
#include "prop_carrier.h"
#include "prop_spawner.h"
#include "spawnable.h"
#include "tool_gun.h"
#include "undo_list.h"

#include <algorithm>
#include <string>
#include <vector>

namespace WorldSave {
namespace {

constexpr std::uint32_t Version = 1;

struct Document {
    std::string scene;
    std::vector<int> spawned;
    void Serialize(Spot::Archive& archive) {
        archive.Field("scene", scene);
        archive.Field("spawned", spawned);
    }
};

bool IsEditable(const Spot::Entity& entity) {
    return !entity.HasComponent<Projectile>() && (entity.HasComponent<Spot::Rigidbody>() ||
                                                  entity.HasComponent<Spawnable>() || entity.HasTag(ToolGun::LampTag));
}

std::vector<Spot::Entity*> GetRoots(Spot::Scene& scene) {
    auto roots = scene.GetRootEntities();
    std::erase_if(roots, [](const Spot::Entity* entity) { return !IsEditable(*entity); });
    return roots;
}

Spot::SaveData MakeStore(const std::filesystem::path& path) {
    return Spot::SaveData(path.has_parent_path() ? path.parent_path() : std::filesystem::current_path());
}

} // namespace

Spot::SaveDataResult Save(Spot::Scene& scene, const std::filesystem::path& path) {
    auto roots = GetRoots(scene);
    Document document;
    document.scene = scene.SaveEntitiesToString(roots, true);
    const auto* spawner = scene.FindComponent<PropSpawner>();
    for (const auto* root : roots) {
        document.spawned.push_back(spawner && spawner->IsSpawned(root->GetId()) ? 1 : 0);
    }
    return MakeStore(path).Save(Spot::PathToUtf8(path.filename()), Version,
                                [&](Spot::Archive& archive, std::uint32_t) { archive.Field("world", document); });
}

Spot::SaveDataResult Load(Spot::Scene& scene, const std::filesystem::path& path, Spot::AssetManager* assets) {
    if (!assets) {
        assets = Spot::Assets::GetManager();
    }
    Document document;
    const auto result =
        MakeStore(path).Load(Spot::PathToUtf8(path.filename()), Version,
                             [&](Spot::Archive& archive, std::uint32_t) { archive.Field("world", document); });
    if (!result) {
        return result;
    }
    {
        Spot::Scene validation;
        if (!validation.LoadFromString(document.scene, assets)) {
            return {Spot::SaveDataStatus::InvalidFile, Version, "The world snapshot is not a scene"};
        }
        const auto roots = validation.GetRootEntities();
        if (roots.size() != document.spawned.size() ||
            std::ranges::any_of(roots, [](const Spot::Entity* root) { return !IsEditable(*root); }) ||
            std::ranges::any_of(document.spawned, [](int flag) { return flag != 0 && flag != 1; })) {
            return {Spot::SaveDataStatus::InvalidFile, Version, "The world snapshot has invalid roots"};
        }
    }
    scene.ForEachComponent<PropCarrier>([](PropCarrier& carrier) { carrier.Drop(); });
    scene.ForEachComponent<PhysicsGun>([](PhysicsGun& gun) { gun.Release(); });
    scene.ForEachComponent<UndoList>([](UndoList& undo) { undo.Clear(); });
    for (auto* root : GetRoots(scene)) {
        root->Destroy();
    }
    auto roots = scene.Instantiate(document.scene, {.keepExternalReferences = true, .assets = assets});
    if (auto* spawner = scene.FindComponent<PropSpawner>()) {
        spawner->ClearTracked();
        for (std::size_t i = 0; i < roots.size(); ++i) {
            if (document.spawned[i] != 0) {
                spawner->TrackSpawned(roots[i]->GetId());
            }
        }
    }
    return result;
}

} // namespace WorldSave
