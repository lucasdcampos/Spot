#pragma once

#include "spot/core/ids/asset_id.h"
#include "spot/core/ids/asset_type.h"
#include "spot/core/ids/entity_id.h"
#include "spot/core/math/color.h"
#include "spot/core/math/curve.h"
#include "spot/core/math/gradient.h"
#include "spot/core/math/quaternion.h"
#include "spot/core/math/vector.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace Spot {

class Archive;

namespace Detail {
struct ArchiveState;
} // namespace Detail

// Types with a Serialize(Archive&) member are saved as nested objects.
template <typename T>
concept ArchiveSerializable = requires(T& value, Archive& archive) { value.Serialize(archive); };

struct FieldOptions;

// Types of other layers describe themselves to archives with a free function found by argument-dependent lookup, as
// the asset layer does for AssetRef and the scene for ComponentRef:
//
//     void SerializeField(Spot::Archive& archive, std::string_view name, MyHandle& value,
//                         const Spot::FieldOptions& options);
template <typename T>
concept ArchiveFieldExtension =
    requires(Archive& archive, std::string_view name, T& value, const FieldOptions& options) {
        SerializeField(archive, name, value, options);
    };

// What tools need to know about a field to edit it well. Files ignore it.
struct FieldOptions {
    // The range numbers stay in when tools change them; none when min is not below max.
    float min = 0.0f;
    float max = 0.0f;
    // How much dragging a number by a pixel changes it; 0 lets tools pick a step from the range or the value.
    float speed = 0.0f;
    // The number is an angle in radians, which tools show in degrees.
    bool angle = false;
    // Colors may go above 1, as light colors scaled by an intensity do.
    bool hdr = false;
    // The names of an enum's values, in the order of their underlying values from 0.
    std::span<const std::string_view> enumNames = {};
    // Explains the field in tools.
    const char* tooltip = nullptr;
    // Tools show the field without letting it be changed.
    bool readOnly = false;
    // Tools do not show the field, as when they edit it their own way. Files still save it.
    bool hidden = false;
    // The type of asset an AssetId field refers to, so tools offer only assets of that type. AssetRef fields know
    // theirs.
    std::optional<AssetType> assetType = std::nullopt;
    // A std::uint32_t with a bit per entity layer (see Entity::GetLayer()), which tools show as layers to tick.
    bool layerMask = false;
    // Tools restrict an entity picker and drag-and-drop to entities accepted by this predicate.
    std::function<bool(EntityId)> entityFilter = {};
    std::string_view componentType = {};
};

// Where an Archive's fields come from and go to. The engine's scene and asset files are one kind; tools add their own,
// such as an inspector that draws a widget for each field and changes the field when the widget is edited.
class ArchiveBackend {
public:
    virtual ~ArchiveBackend() = default;

    // Whether fields may be given new values, as when loading, rather than only read, as when saving.
    virtual bool IsLoading() const = 0;

    virtual void Field(std::string_view name, bool& value, const FieldOptions& options) = 0;
    virtual void Field(std::string_view name, int& value, const FieldOptions& options) = 0;
    virtual void Field(std::string_view name, std::uint32_t& value, const FieldOptions& options) = 0;
    virtual void Field(std::string_view name, std::int64_t& value, const FieldOptions& options) = 0;
    virtual void Field(std::string_view name, std::uint64_t& value, const FieldOptions& options) = 0;
    virtual void Field(std::string_view name, float& value, const FieldOptions& options) = 0;
    virtual void Field(std::string_view name, double& value, const FieldOptions& options) = 0;
    virtual void Field(std::string_view name, std::string& value, const FieldOptions& options) = 0;
    virtual void Field(std::string_view name, Vector2& value, const FieldOptions& options) = 0;
    virtual void Field(std::string_view name, Vector3& value, const FieldOptions& options) = 0;
    virtual void Field(std::string_view name, Vector4& value, const FieldOptions& options) = 0;
    virtual void Field(std::string_view name, Quaternion& value, const FieldOptions& options) = 0;
    virtual void Field(std::string_view name, Color& value, const FieldOptions& options) = 0;
    virtual void Field(std::string_view name, EntityId& value, const FieldOptions& options) = 0;
    // The type, when known, is the type of asset the id must be.
    virtual void Field(std::string_view name, AssetId& value, std::optional<AssetType> type,
                       const FieldOptions& options) = 0;
    // Enums, as their underlying value; options name their values when they can.
    virtual void EnumField(std::string_view name, std::int64_t& value, const FieldOptions& options) {
        Field(name, value, options);
    }

    // Specialized tools may draw previews; the default visits the same portable key fields.
    virtual void CurveField(std::string_view name, Curve& value, const FieldOptions& options);
    virtual void GradientField(std::string_view name, Gradient& value, const FieldOptions& options);

    // Nested objects and arrays. When Begin...() returns false the field is skipped, and the matching End...() is
    // not called. Arrays give their size, which a loading backend may change.
    virtual bool BeginObject(std::string_view name, const FieldOptions& options) = 0;
    virtual void EndObject() = 0;
    virtual bool BeginArray(std::string_view name, std::size_t& size, const FieldOptions& options) = 0;
    virtual void BeginElement(std::size_t index) = 0;
    virtual void EndElement() = 0;
    virtual void EndArray() = 0;

    // Supplies a service to field extensions without making the core depend on its type.
    virtual void* GetContext(const std::type_info& /*type*/) const { return nullptr; }
};

// Saves or loads the fields of a component when its scene is saved or loaded, and lets tools edit them. The same
// function does all of it, so fields cannot get out of step:
//
//     void Serialize(Spot::Archive& archive) override {
//         archive.Field("speed", m_speed, {.min = 0.0f, .tooltip = "Units per second"});
//         archive.Field("target", m_target);
//     }
//
// When loading, a missing field, or one of another type, leaves the value as it is, so fields added to a component
// later keep their defaults in older files. Field names must be unique within an object.
class Archive {
public:
    explicit Archive(ArchiveBackend& backend) : m_backend(&backend) {}
    // Archives of the engine's files are made by the engine when it saves or loads them.
    explicit Archive(Detail::ArchiveState& state);
    ~Archive();

    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    bool IsLoading() const { return m_backend->IsLoading(); }
    bool IsSaving() const { return !IsLoading(); }

    void Field(std::string_view name, bool& value, const FieldOptions& options = {});
    void Field(std::string_view name, int& value, const FieldOptions& options = {});
    void Field(std::string_view name, std::uint32_t& value, const FieldOptions& options = {});
    void Field(std::string_view name, std::int64_t& value, const FieldOptions& options = {});
    void Field(std::string_view name, std::uint64_t& value, const FieldOptions& options = {});
    void Field(std::string_view name, float& value, const FieldOptions& options = {});
    void Field(std::string_view name, double& value, const FieldOptions& options = {});
    void Field(std::string_view name, std::string& value, const FieldOptions& options = {});
    void Field(std::string_view name, Vector2& value, const FieldOptions& options = {});
    void Field(std::string_view name, Vector3& value, const FieldOptions& options = {});
    void Field(std::string_view name, Vector4& value, const FieldOptions& options = {});
    void Field(std::string_view name, Quaternion& value, const FieldOptions& options = {});
    void Field(std::string_view name, Color& value, const FieldOptions& options = {});
    void Field(std::string_view name, Curve& value, const FieldOptions& options = {});
    void Field(std::string_view name, Gradient& value, const FieldOptions& options = {});
    // References to other entities. Entities loaded together keep referring to each other through their new ids;
    // references to entities that were not saved along load as EntityId::Invalid.
    void Field(std::string_view name, EntityId& value, const FieldOptions& options = {});
    // Saved as 32 hexadecimal digits, or null for the null id.
    void Field(std::string_view name, AssetId& value, const FieldOptions& options = {});

    // Enums are saved as their underlying value.
    template <typename T>
        requires std::is_enum_v<T>
    void Field(std::string_view name, T& value, const FieldOptions& options = {}) {
        auto underlying = static_cast<std::int64_t>(value);
        m_backend->EnumField(name, underlying, options);
        value = static_cast<T>(underlying);
    }

    template <ArchiveFieldExtension T>
    void Field(std::string_view name, T& value, const FieldOptions& options = {}) {
        SerializeField(*this, name, value, options);
    }

    template <ArchiveSerializable T>
    void Field(std::string_view name, T& value, const FieldOptions& options = {}) {
        if (m_backend->BeginObject(name, options)) {
            value.Serialize(*this);
            m_backend->EndObject();
        }
    }

    // When loading, the vector is resized to the saved one, so T must be default-constructible.
    template <typename T>
    void Field(std::string_view name, std::vector<T>& values, const FieldOptions& options = {}) {
        std::size_t size = values.size();
        if (!m_backend->BeginArray(name, size, options)) {
            return;
        }
        if (IsLoading()) {
            values.resize(size);
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            m_backend->BeginElement(i);
            Field({}, values[i]);
            m_backend->EndElement();
        }
        m_backend->EndArray();
    }

    template <typename T>
    T* GetContext() const {
        return static_cast<T*>(m_backend->GetContext(typeid(T)));
    }

private:
    std::unique_ptr<ArchiveBackend> m_ownedBackend;
    ArchiveBackend* m_backend;
};

namespace Detail {

// Describes each field to the archive under its name, the names coming as SPOT_FIELDS writes them: "speed, health".
template <typename... Fields>
void SerializeFields(Archive& archive, std::string_view names, Fields&... fields) {
    const auto next = [&names] {
        const std::size_t comma = names.find(',');
        std::string_view name = names.substr(0, comma);
        names = comma == std::string_view::npos ? std::string_view() : names.substr(comma + 1);
        constexpr std::string_view Space = " \t\r\n";
        const std::size_t first = name.find_first_not_of(Space);
        name.remove_prefix(first == std::string_view::npos ? name.size() : first);
        name.remove_suffix(name.size() - (name.find_last_not_of(Space) + 1));
        return name;
    };
    (archive.Field(next(), fields), ...);
}

} // namespace Detail

} // namespace Spot

// Saves the fields with scenes and shows them in the inspector, each under its own name, so the component needs no
// Serialize() of its own. Written in the class, after the fields:
//
//     float speed = 5.0f;
//     int health = 100;
//
//     SPOT_FIELDS(speed, health)
//
// Fields that need options, such as a range or a tooltip, are described in a Serialize() instead (see Archive).
#define SPOT_FIELDS(...)                                                                                               \
    void Serialize(::Spot::Archive& archive) override {                                                                \
        ::Spot::Detail::SerializeFields(archive, #__VA_ARGS__, __VA_ARGS__);                                           \
    }
