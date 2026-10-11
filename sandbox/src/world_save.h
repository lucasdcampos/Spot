#pragma once

#include <spot.h>

namespace WorldSave {

// Checkpoints the editable roots and their descendants, including joints and mounted lamps. The player, level,
// ambience, projectiles and flashes remain live. Loading releases held props and clears obsolete undo history.
Spot::SaveDataResult Save(Spot::Scene& scene, const std::filesystem::path& path);
Spot::SaveDataResult Load(Spot::Scene& scene, const std::filesystem::path& path, Spot::AssetManager* assets = nullptr);

} // namespace WorldSave
