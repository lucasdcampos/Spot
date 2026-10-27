#include "gameplay/persistence/world_save.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Gameplay {

SPOT_COMPONENT(Persistent);
SPOT_COMPONENT(Transient);

namespace WorldSave {

namespace {

constexpr std::uint32_t Version = 2;

struct Document {
    std::string scene;
    void Serialize(Spot::Archive& archive) { archive.Field("scene", scene); }
};

std::vector<Spot::Entity*> GetRoots(Spot::Scene& scene) {
    auto roots = scene.GetRootEntities();
    std::erase_if(roots, [](const Spot::Entity* entity) { return !IsSaved(*entity); });
    return roots;
}

std::vector<SaveListener*> GetListeners(Spot::Scene& scene) {
    std::vector<SaveListener*> listeners;
    scene.ForEachComponent<Spot::Component>([&](Spot::Component& component) {
        if (auto* listener = dynamic_cast<SaveListener*>(&component)) {
            listeners.push_back(listener);
        }
    });
    return listeners;
}

Spot::SaveData MakeStore(const std::filesystem::path& path) {
    return Spot::SaveData(path.has_parent_path() ? path.parent_path() : std::filesystem::current_path());
}

} // namespace

bool IsSaved(const Spot::Entity& root) {
    return root.HasComponent<Persistent>() && !root.HasComponent<Transient>();
}

Spot::SaveDataResult Save(Spot::Scene& scene, const std::filesystem::path& path) {
    Document document;
    document.scene = scene.SaveEntitiesToString(GetRoots(scene), true);
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
        if (std::ranges::any_of(roots, [](const Spot::Entity* root) { return !IsSaved(*root); })) {
            return {Spot::SaveDataStatus::InvalidFile, Version, "The world snapshot has invalid roots"};
        }
    }
    for (SaveListener* listener : GetListeners(scene)) {
        listener->OnBeforeLoad();
    }
    for (Spot::Entity* root : GetRoots(scene)) {
        root->Destroy();
    }
    const auto roots = scene.Instantiate(document.scene, {.keepExternalReferences = true, .assets = assets});
    for (SaveListener* listener : GetListeners(scene)) {
        listener->OnAfterLoad(roots);
    }
    return result;
}

} // namespace WorldSave

} // namespace Gameplay
