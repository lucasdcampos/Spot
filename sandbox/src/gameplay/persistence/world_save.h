#pragma once

#include <spot.h>

#include <filesystem>
#include <span>

namespace Gameplay {

// Marks a root entity as part of the world a save keeps: what the player made, moved or broke. Everything else, the
// level, the player and the ambience, stays as it is when a save loads.
class Persistent : public Spot::Component {};

// Keeps a root entity out of saves even when it is Persistent, as a rocket in flight.
class Transient : public Spot::Component {};

// A component that holds on to entities a load may destroy, or keeps track of them: it lets go before the load, as a
// carrier drops what it carries, and finds them again after it.
class SaveListener {
public:
    virtual ~SaveListener() = default;

    // Before the persistent roots are destroyed and the save's are made.
    virtual void OnBeforeLoad() {}
    // The roots the save made, in the order they were saved.
    virtual void OnAfterLoad(std::span<Spot::Entity* const> /*roots*/) {}
};

// Checkpoints the world: the persistent roots of a scene, with what is below them, into a file, and back.
namespace WorldSave {

// Whether the root entity is saved: Persistent and not Transient.
bool IsSaved(const Spot::Entity& root);

Spot::SaveDataResult Save(Spot::Scene& scene, const std::filesystem::path& path);
// Replaces the persistent roots with those saved, telling the scene's SaveListeners before and after.
Spot::SaveDataResult Load(Spot::Scene& scene, const std::filesystem::path& path, Spot::AssetManager* assets = nullptr);

} // namespace WorldSave

} // namespace Gameplay
