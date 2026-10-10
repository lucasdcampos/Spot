#include "scene/archive.h"

#include "core/log.h"
#include "scene/archive_state.h"
#include "scene/scene.h"

#include <algorithm>
#include <array>

namespace Spot {

using nlohmann::json;

namespace {

json* FindField(Detail::ArchiveState& state, std::string_view name) {
    json& node = *state.nodes.back();
    if (name.empty()) {
        return &node;
    }
    if (!node.is_object()) {
        return nullptr;
    }
    const auto found = node.find(name);
    return found != node.end() ? &*found : nullptr;
}

json& AddField(Detail::ArchiveState& state, std::string_view name) {
    json& node = *state.nodes.back();
    return name.empty() ? node : node[std::string(name)];
}

void WarnWrongType(std::string_view name, const char* expected) {
    Log::GetEngineLogger().Warning("Field '{}' should be {}; it keeps its value", name, expected);
}

template <typename T>
void Scalar(Detail::ArchiveState& state, std::string_view name, T& value, bool (json::*matches)() const noexcept,
            const char* expected) {
    if (!state.loading) {
        AddField(state, name) = value;
        return;
    }
    const json* field = FindField(state, name);
    if (!field) {
        return;
    }
    if (!(field->*matches)()) {
        WarnWrongType(name, expected);
        return;
    }
    value = field->get<T>();
}

// Vectors, quaternions and colors are arrays of floats.
template <std::size_t N>
void Floats(Detail::ArchiveState& state, std::string_view name, std::array<float*, N> values) {
    if (!state.loading) {
        json& field = AddField(state, name);
        field = json::array();
        for (const float* value : values) {
            field.push_back(*value);
        }
        return;
    }
    const json* field = FindField(state, name);
    if (!field) {
        return;
    }
    if (!field->is_array() || field->size() != N ||
        !std::ranges::all_of(*field, [](const json& element) { return element.is_number(); })) {
        WarnWrongType(name, "an array of numbers of the right size");
        return;
    }
    for (std::size_t i = 0; i < N; ++i) {
        *values[i] = (*field)[i].get<float>();
    }
}

// The engine's files: fields as members of JSON objects.
class JsonBackend final : public ArchiveBackend {
public:
    explicit JsonBackend(Detail::ArchiveState& state) : m_state(state) {}

    bool IsLoading() const override { return m_state.loading; }

    void Field(std::string_view name, bool& value, const FieldOptions&) override {
        Scalar(m_state, name, value, &json::is_boolean, "a boolean");
    }

    void Field(std::string_view name, int& value, const FieldOptions&) override {
        Scalar(m_state, name, value, &json::is_number_integer, "an integer");
    }

    void Field(std::string_view name, std::uint32_t& value, const FieldOptions&) override {
        Scalar(m_state, name, value, &json::is_number_unsigned, "an unsigned integer");
    }

    void Field(std::string_view name, std::int64_t& value, const FieldOptions&) override {
        Scalar(m_state, name, value, &json::is_number_integer, "an integer");
    }

    void Field(std::string_view name, std::uint64_t& value, const FieldOptions&) override {
        Scalar(m_state, name, value, &json::is_number_unsigned, "an unsigned integer");
    }

    void Field(std::string_view name, float& value, const FieldOptions&) override {
        Scalar(m_state, name, value, &json::is_number, "a number");
    }

    void Field(std::string_view name, double& value, const FieldOptions&) override {
        Scalar(m_state, name, value, &json::is_number, "a number");
    }

    void Field(std::string_view name, std::string& value, const FieldOptions&) override {
        Scalar(m_state, name, value, &json::is_string, "a string");
    }

    void Field(std::string_view name, Vector2& value, const FieldOptions&) override {
        Floats<2>(m_state, name, {&value.x, &value.y});
    }

    void Field(std::string_view name, Vector3& value, const FieldOptions&) override {
        Floats<3>(m_state, name, {&value.x, &value.y, &value.z});
    }

    void Field(std::string_view name, Vector4& value, const FieldOptions&) override {
        Floats<4>(m_state, name, {&value.x, &value.y, &value.z, &value.w});
    }

    void Field(std::string_view name, Quaternion& value, const FieldOptions&) override {
        Floats<4>(m_state, name, {&value.x, &value.y, &value.z, &value.w});
    }

    void Field(std::string_view name, Color& value, const FieldOptions&) override {
        Floats<4>(m_state, name, {&value.r, &value.g, &value.b, &value.a});
    }

    void Field(std::string_view name, EntityId& value, const FieldOptions& options) override {
        auto id = static_cast<std::uint64_t>(value);
        if (!m_state.loading && m_state.prefabIds) {
            const auto found = m_state.prefabIds->find(id);
            AddField(m_state, name) = found != m_state.prefabIds->end()
                                          ? json{{"prefabEntity", static_cast<std::uint64_t>(found->second)}}
                                          : json{{"sceneEntity", id}};
            return;
        }
        if (m_state.loading) {
            const json* field = FindField(m_state, name);
            if (field && field->is_object()) {
                const auto local = field->find("prefabEntity");
                const auto external = field->find("sceneEntity");
                const json* saved = local != field->end() ? &*local : external != field->end() ? &*external : nullptr;
                if (!saved || field->size() != 1 || !saved->is_number_unsigned()) {
                    WarnWrongType(name, "an entity reference");
                    return;
                }
                id = saved->get<std::uint64_t>();
                const auto* ids = local != field->end() ? m_state.entityIds : m_state.externalEntityIds;
                if (ids) {
                    const auto found = ids->find(id);
                    value = found != ids->end() ? found->second : EntityId::Invalid;
                } else {
                    value = static_cast<EntityId>(id);
                }
                if (external != field->end() && value == EntityId::Invalid && m_state.scene &&
                    m_state.scene->GetEntity(static_cast<EntityId>(id))) {
                    value = static_cast<EntityId>(id);
                }
                return;
            }
        }
        Field(name, id, options);
        if (!m_state.loading || !m_state.entityIds) {
            value = static_cast<EntityId>(id);
            return;
        }
        const auto found = m_state.entityIds->find(id);
        if (found != m_state.entityIds->end()) {
            value = found->second;
        } else if (m_state.scene && m_state.scene->GetEntity(static_cast<EntityId>(id))) {
            value = static_cast<EntityId>(id);
        } else {
            value = EntityId::Invalid;
        }
    }

    void Field(std::string_view name, AssetId& value, std::optional<AssetType>, const FieldOptions&) override {
        if (!m_state.loading) {
            AddField(m_state, name) = value.IsValid() ? json(value.ToString()) : json(nullptr);
            return;
        }
        const json* field = FindField(m_state, name);
        if (!field) {
            return;
        }
        if (field->is_null()) {
            value = {};
            return;
        }
        const auto id = field->is_string() ? AssetId::Parse(field->get<std::string>()) : std::nullopt;
        if (!id) {
            WarnWrongType(name, "an asset id of 32 hexadecimal digits");
            return;
        }
        value = *id;
    }

    bool BeginObject(std::string_view name, const FieldOptions&) override {
        std::size_t size = 0;
        return Enter(name, false, size);
    }

    void EndObject() override { m_state.nodes.pop_back(); }

    bool BeginArray(std::string_view name, std::size_t& size, const FieldOptions&) override {
        return Enter(name, true, size);
    }

    void BeginElement(std::size_t index) override { m_state.nodes.push_back(&(*m_state.nodes.back())[index]); }

    void EndElement() override { m_state.nodes.pop_back(); }

    void EndArray() override { m_state.nodes.pop_back(); }

    AssetManager* GetAssetManager() const override { return m_state.assets; }

private:
    // Makes the named field the current node, or the current node itself when the name is empty. When loading,
    // returns false if it is missing or of another kind, and gives the size of arrays.
    bool Enter(std::string_view name, bool array, std::size_t& size) {
        if (!m_state.loading) {
            json& field = AddField(m_state, name);
            field = array ? json(size, nullptr) : json::object();
            m_state.nodes.push_back(&field);
            return true;
        }
        json* field = FindField(m_state, name);
        if (!field) {
            return false;
        }
        if (array ? !field->is_array() : !field->is_object()) {
            WarnWrongType(name, array ? "an array" : "an object");
            return false;
        }
        size = field->size();
        m_state.nodes.push_back(field);
        return true;
    }

    Detail::ArchiveState& m_state;
};

} // namespace

Archive::Archive(Detail::ArchiveState& state)
    : m_ownedBackend(std::make_unique<JsonBackend>(state)), m_backend(m_ownedBackend.get()) {
}

Archive::~Archive() = default;

void Archive::Field(std::string_view name, bool& value, const FieldOptions& options) {
    m_backend->Field(name, value, options);
}

void Archive::Field(std::string_view name, int& value, const FieldOptions& options) {
    m_backend->Field(name, value, options);
}

void Archive::Field(std::string_view name, std::uint32_t& value, const FieldOptions& options) {
    m_backend->Field(name, value, options);
}

void Archive::Field(std::string_view name, std::int64_t& value, const FieldOptions& options) {
    m_backend->Field(name, value, options);
}

void Archive::Field(std::string_view name, std::uint64_t& value, const FieldOptions& options) {
    m_backend->Field(name, value, options);
}

void Archive::Field(std::string_view name, float& value, const FieldOptions& options) {
    m_backend->Field(name, value, options);
}

void Archive::Field(std::string_view name, double& value, const FieldOptions& options) {
    m_backend->Field(name, value, options);
}

void Archive::Field(std::string_view name, std::string& value, const FieldOptions& options) {
    m_backend->Field(name, value, options);
}

void Archive::Field(std::string_view name, Vector2& value, const FieldOptions& options) {
    m_backend->Field(name, value, options);
}

void Archive::Field(std::string_view name, Vector3& value, const FieldOptions& options) {
    m_backend->Field(name, value, options);
}

void Archive::Field(std::string_view name, Vector4& value, const FieldOptions& options) {
    m_backend->Field(name, value, options);
}

void Archive::Field(std::string_view name, Quaternion& value, const FieldOptions& options) {
    m_backend->Field(name, value, options);
}

void Archive::Field(std::string_view name, Color& value, const FieldOptions& options) {
    m_backend->Field(name, value, options);
}

void Archive::Field(std::string_view name, EntityId& value, const FieldOptions& options) {
    m_backend->Field(name, value, options);
}

void Archive::Field(std::string_view name, AssetId& value, const FieldOptions& options) {
    m_backend->Field(name, value, options.assetType, options);
}

AssetManager* Archive::GetAssetManager() const {
    AssetManager* assets = m_backend->GetAssetManager();
    return assets ? assets : Assets::GetManager();
}

void Archive::WarnAssetWithoutId(std::string_view name) {
    Log::GetEngineLogger().Warning("Field '{}' refers to an asset made in code, which is saved as null", name);
}

} // namespace Spot
