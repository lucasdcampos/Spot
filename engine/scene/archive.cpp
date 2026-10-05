#include "scene/archive.h"

#include "core/log.h"
#include "scene/archive_state.h"

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

} // namespace

bool Archive::IsLoading() const {
    return m_state.loading;
}

void Archive::Field(std::string_view name, bool& value) {
    Scalar(m_state, name, value, &json::is_boolean, "a boolean");
}

void Archive::Field(std::string_view name, int& value) {
    Scalar(m_state, name, value, &json::is_number_integer, "an integer");
}

void Archive::Field(std::string_view name, std::uint32_t& value) {
    Scalar(m_state, name, value, &json::is_number_unsigned, "an unsigned integer");
}

void Archive::Field(std::string_view name, std::int64_t& value) {
    Scalar(m_state, name, value, &json::is_number_integer, "an integer");
}

void Archive::Field(std::string_view name, std::uint64_t& value) {
    Scalar(m_state, name, value, &json::is_number_unsigned, "an unsigned integer");
}

void Archive::Field(std::string_view name, float& value) {
    Scalar(m_state, name, value, &json::is_number, "a number");
}

void Archive::Field(std::string_view name, double& value) {
    Scalar(m_state, name, value, &json::is_number, "a number");
}

void Archive::Field(std::string_view name, std::string& value) {
    Scalar(m_state, name, value, &json::is_string, "a string");
}

void Archive::Field(std::string_view name, Vector2& value) {
    Floats<2>(m_state, name, {&value.x, &value.y});
}

void Archive::Field(std::string_view name, Vector3& value) {
    Floats<3>(m_state, name, {&value.x, &value.y, &value.z});
}

void Archive::Field(std::string_view name, Vector4& value) {
    Floats<4>(m_state, name, {&value.x, &value.y, &value.z, &value.w});
}

void Archive::Field(std::string_view name, Quaternion& value) {
    Floats<4>(m_state, name, {&value.x, &value.y, &value.z, &value.w});
}

void Archive::Field(std::string_view name, Color& value) {
    Floats<4>(m_state, name, {&value.r, &value.g, &value.b, &value.a});
}

void Archive::Field(std::string_view name, EntityId& value) {
    auto id = static_cast<std::uint64_t>(value);
    Field(name, id);
    if (!IsLoading() || !m_state.entityIds) {
        value = static_cast<EntityId>(id);
        return;
    }
    const auto found = m_state.entityIds->find(id);
    value = found != m_state.entityIds->end() ? found->second : EntityId::Invalid;
}

void Archive::Field(std::string_view name, AssetId& value) {
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

bool Archive::Enter(std::string_view name, NodeKind kind, std::size_t& size) {
    if (!m_state.loading) {
        json& field = AddField(m_state, name);
        field = kind == NodeKind::Object ? json::object() : json(size, nullptr);
        m_state.nodes.push_back(&field);
        return true;
    }
    json* field = FindField(m_state, name);
    if (!field) {
        return false;
    }
    if (kind == NodeKind::Object ? !field->is_object() : !field->is_array()) {
        WarnWrongType(name, kind == NodeKind::Object ? "an object" : "an array");
        return false;
    }
    size = field->size();
    m_state.nodes.push_back(field);
    return true;
}

void Archive::EnterElement(std::size_t index) {
    m_state.nodes.push_back(&(*m_state.nodes.back())[index]);
}

void Archive::Leave() {
    m_state.nodes.pop_back();
}

} // namespace Spot
