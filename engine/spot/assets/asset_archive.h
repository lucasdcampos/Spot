#pragma once

#include "spot/assets/asset_manager.h"
#include "spot/core/archive.h"
#include "spot/core/asset_ref.h"

#include <string_view>

namespace Spot {

namespace Detail {

void WarnAssetWithoutId(std::string_view name);

} // namespace Detail

// References load only through the manager supplied by the archive's context. Without one, their ids are retained.
// Assets made in code have no id, so they are saved as null, with a warning.
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
        AssetManager* assets = archive.GetContext<AssetManager>();
        value = !id.IsValid() ? AssetRef<T>() : assets ? assets->Load<T>(id) : AssetRef<T>(id, nullptr);
    }
}

} // namespace Spot
