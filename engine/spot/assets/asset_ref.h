#pragma once

#include "spot/core/ids/asset_id.h"

#include <concepts>
#include <memory>
#include <utility>

namespace Spot {

// A shared reference to a loaded asset, which remembers the asset's id so scenes can save it. Assets stay loaded
// while something refers to them. Assets made in code can be referred to as well; they have no id, so they are
// not saved.
//
// A reference can have an id without an asset, when loading it failed or there was no asset manager to load it
// with; saving keeps the id, so the reference is not lost.
template <typename T>
class AssetRef {
public:
    AssetRef() = default;

    template <typename U>
        requires std::convertible_to<std::shared_ptr<U>, std::shared_ptr<const T>>
    AssetRef(std::shared_ptr<U> asset) : m_asset(std::move(asset)) {}

    template <typename U>
        requires std::convertible_to<std::unique_ptr<U>, std::shared_ptr<const T>>
    AssetRef(std::unique_ptr<U> asset) : m_asset(std::move(asset)) {}

    AssetRef(AssetId id, std::shared_ptr<const T> asset) : m_id(id), m_asset(std::move(asset)) {}

    AssetId GetId() const { return m_id; }
    const T* Get() const { return m_asset.get(); }
    const std::shared_ptr<const T>& GetShared() const { return m_asset; }

    const T& operator*() const { return *m_asset; }
    const T* operator->() const { return m_asset.get(); }
    explicit operator bool() const { return m_asset != nullptr; }

    friend bool operator==(const AssetRef& a, const AssetRef& b) { return a.m_id == b.m_id && a.m_asset == b.m_asset; }

private:
    AssetId m_id;
    std::shared_ptr<const T> m_asset;
};

} // namespace Spot
