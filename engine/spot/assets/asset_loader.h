#pragma once

#include "spot/core/asset_id.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace Spot {
class AssetManager;

struct AssetLoader {
    // Decode CPU data on a worker; callbacks must not touch graphics or the manager.
    std::function<std::shared_ptr<void>(AssetId, std::span<const std::byte>)> prepare = {};
    // Resolve references and create resources on the thread calling Load, Update or FinishPreload.
    std::function<std::shared_ptr<const void>(AssetManager&, AssetId, void*)> create = {};
    std::function<std::vector<AssetId>(const void*)> dependencies = {};
    std::function<std::vector<AssetId>(const void*)> references = {};
    bool textReferences = false;
};

template <typename T, typename Decoder, typename Factory>
AssetLoader MakeAssetLoader(Decoder decode, Factory create) {
    return {
        .prepare = [decode = std::move(decode)](AssetId id, std::span<const std::byte> data) -> std::shared_ptr<void> {
            auto value = decode(id, data);
            return value ? std::make_shared<T>(std::move(*value)) : nullptr;
        },
        .create = [create = std::move(create)](AssetManager& assets, AssetId id, void* data)
            -> std::shared_ptr<const void> { return create(assets, id, *static_cast<T*>(data)); }};
}
} // namespace Spot
