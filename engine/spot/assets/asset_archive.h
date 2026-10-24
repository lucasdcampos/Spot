#pragma once

#include "spot/assets/asset_manager.h"
#include "spot/assets/asset_ref.h"
#include "spot/assets/assets.h"
#include "spot/core/io/archive.h"

#include <string_view>

namespace Spot {

namespace Detail {

void WarnAssetWithoutId(std::string_view name);

} // namespace Detail

// References to assets are saved as their id, and loaded through the archive's asset manager, or the running
// application's. Assets made in code have no id, so they are saved as null, with a warning.
template <LoadableAsset T>
void SerializeField(Archive& archive, std::string_view name, AssetRef<T>& value, const FieldOptions& options) {
    AssetId id = value.GetId();
    if (archive.IsSaving() && !id.IsValid() && value) {
        Detail::WarnAssetWithoutId(name);
    }
    FieldOptions typed = options;
    typed.assetType = AssetTypeOf<T>::Value;
    archive.Field(name, id, typed);
    if (archive.IsLoading() && id != value.GetId()) {
        AssetManager* assets = archive.GetAssetManager();
        assets = assets ? assets : Assets::GetManager();
        value = !id.IsValid() ? AssetRef<T>() : assets ? assets->Load<T>(id) : AssetRef<T>(id, nullptr);
    }
}

} // namespace Spot
