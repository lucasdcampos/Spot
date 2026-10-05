#pragma once

#include "assets/asset_id.h"
#include "assets/asset_ref.h"
#include "assets/assets.h"
#include "graphics/color.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "scene/entity.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Spot {

class Archive;

namespace Detail {
struct ArchiveState;
} // namespace Detail

// Types with a Serialize(Archive&) member are saved as nested objects.
template <typename T>
concept ArchiveSerializable = requires(T& value, Archive& archive) { value.Serialize(archive); };

// Saves or loads the fields of a component when its scene is saved or loaded. The same function does both, so
// fields cannot get out of step:
//
//     void Serialize(Spot::Archive& archive) override {
//         archive.Field("speed", m_speed);
//         archive.Field("target", m_target);
//     }
//
// When loading, a missing field, or one of another type, leaves the value as it is, so fields added to a component
// later keep their defaults in older files. Field names must be unique within an object.
class Archive {
public:
    // Archives are made by the engine when it saves or loads a scene.
    explicit Archive(Detail::ArchiveState& state) : m_state(state) {}

    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    bool IsLoading() const;
    bool IsSaving() const { return !IsLoading(); }

    void Field(std::string_view name, bool& value);
    void Field(std::string_view name, int& value);
    void Field(std::string_view name, std::uint32_t& value);
    void Field(std::string_view name, std::int64_t& value);
    void Field(std::string_view name, std::uint64_t& value);
    void Field(std::string_view name, float& value);
    void Field(std::string_view name, double& value);
    void Field(std::string_view name, std::string& value);
    void Field(std::string_view name, Vector2& value);
    void Field(std::string_view name, Vector3& value);
    void Field(std::string_view name, Vector4& value);
    void Field(std::string_view name, Quaternion& value);
    void Field(std::string_view name, Color& value);
    // References to other entities. Entities loaded together keep referring to each other through their new ids;
    // references to entities that were not saved along load as EntityId::Invalid.
    void Field(std::string_view name, EntityId& value);
    // Saved as 32 hexadecimal digits, or null for the null id.
    void Field(std::string_view name, AssetId& value);

    // References to assets are saved as their id, and loaded through the asset manager the scene is loaded with,
    // or the running application's. Assets made in code have no id, so they are saved as null, with a warning.
    template <LoadableAsset T>
    void Field(std::string_view name, AssetRef<T>& value) {
        AssetId id = value.GetId();
        if (IsSaving() && !id.IsValid() && value) {
            WarnAssetWithoutId(name);
        }
        Field(name, id);
        if (IsLoading() && id != value.GetId()) {
            AssetManager* assets = GetAssetManager();
            value = !id.IsValid() ? AssetRef<T>() : assets ? assets->Load<T>(id) : AssetRef<T>(id, nullptr);
        }
    }

    // Enums are saved as their underlying value.
    template <typename T>
        requires std::is_enum_v<T>
    void Field(std::string_view name, T& value) {
        auto underlying = static_cast<std::int64_t>(value);
        Field(name, underlying);
        value = static_cast<T>(underlying);
    }

    template <ArchiveSerializable T>
    void Field(std::string_view name, T& value) {
        std::size_t size = 0;
        if (Enter(name, NodeKind::Object, size)) {
            value.Serialize(*this);
            Leave();
        }
    }

    // When loading, the vector is resized to the saved one, so T must be default-constructible.
    template <typename T>
    void Field(std::string_view name, std::vector<T>& values) {
        std::size_t size = values.size();
        if (!Enter(name, NodeKind::Array, size)) {
            return;
        }
        if (IsLoading()) {
            values.resize(size);
        }
        for (std::size_t i = 0; i < size; ++i) {
            EnterElement(i);
            Field({}, values[i]);
            Leave();
        }
        Leave();
    }

private:
    enum class NodeKind { Object, Array };

    AssetManager* GetAssetManager() const;
    static void WarnAssetWithoutId(std::string_view name);

    // Makes the named field the current node, or the current node itself when the name is empty. When loading,
    // returns false if it is missing or of another kind, and gives the size of arrays.
    bool Enter(std::string_view name, NodeKind kind, std::size_t& size);
    void EnterElement(std::size_t index);
    void Leave();

    Detail::ArchiveState& m_state;
};

} // namespace Spot
