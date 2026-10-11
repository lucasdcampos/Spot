#include "inspector/inspector_backend.h"

#include "assets/asset_list.h"
#include "core/text.h"
#include "imgui/drag_payloads.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/widgets.h"
#include "inspector/multi_edit.h"
#include "inspector/property_widgets.h"
#include "project/editor_project.h"
#include "scene/entity_actions.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstring>
#include <format>
#include <type_traits>
#include <utility>

namespace Spot::Editor {

namespace {

constexpr const char* DegreesFormat = "%.1f\xC2\xB0";

template <typename T>
constexpr ImGuiDataType GetDataType() {
    if constexpr (std::is_same_v<T, int>) {
        return ImGuiDataType_S32;
    } else if constexpr (std::is_same_v<T, std::uint32_t>) {
        return ImGuiDataType_U32;
    } else if constexpr (std::is_same_v<T, std::int64_t>) {
        return ImGuiDataType_S64;
    } else if constexpr (std::is_same_v<T, std::uint64_t>) {
        return ImGuiDataType_U64;
    } else if constexpr (std::is_same_v<T, float>) {
        return ImGuiDataType_Float;
    } else {
        static_assert(std::is_same_v<T, double>);
        return ImGuiDataType_Double;
    }
}

template <typename T>
T ToBound(float bound) {
    if constexpr (std::is_unsigned_v<T>) {
        return static_cast<T>(std::max(bound, 0.0f));
    } else {
        return static_cast<T>(bound);
    }
}

float GetSpeed(const FieldOptions& options, bool integer) {
    if (options.speed > 0.0f) {
        return options.speed;
    }
    if (options.min < options.max) {
        return std::max((options.max - options.min) / 300.0f, integer ? 0.05f : 0.0001f);
    }
    return integer ? 0.1f : 0.01f;
}

ImGuiSliderFlags GetSliderFlags(const FieldOptions& options) {
    return options.min < options.max ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
}

// The entity and its descendants, in hierarchy order, with the names of their parents as details.
void ListEntities(Entity& entity, const std::string& parents, std::vector<PickerItem>& items,
                  std::vector<EntityId>& ids, bool (*filter)(const Entity&)) {
    if (!filter || filter(entity)) {
        items.push_back({entity.GetName(), GetEntityIcon(entity), parents});
        ids.push_back(entity.GetId());
    }
    const std::string path = parents.empty() ? entity.GetName() : parents + "/" + entity.GetName();
    for (Entity* child : entity.GetChildren()) {
        ListEntities(*child, path, items, ids, filter);
    }
}

// Where an asset is, for telling apart assets of the same name.
std::string GetAssetDetail(const AssetEntry& entry) {
    if (entry.builtIn) {
        return "Built-in";
    }
    if (!entry.path.ends_with(entry.name)) {
        return entry.path;
    }
    const std::size_t slash = entry.path.find_last_of('/');
    return slash == std::string::npos ? std::string() : entry.path.substr(0, slash);
}

} // namespace

InspectorBackend::InspectorBackend(Options options) : m_options(std::move(options)), m_path(m_options.root) {
}

std::string InspectorBackend::GetPointer(std::string_view name) const {
    return name.empty() ? m_path : m_path + "/" + EscapePointerToken(name);
}

std::string InspectorBackend::GetLabel(std::string_view name) const {
    if (!name.empty()) {
        return MakeDisplayName(name);
    }
    return m_element ? "Element " + std::to_string(*m_element) : "Value";
}

bool InspectorBackend::IsMixed(std::string_view pointer) const {
    return std::ranges::any_of(m_options.mixed, [&](const std::string& mixed) {
        return mixed.starts_with(pointer) && (mixed.size() == pointer.size() || mixed[pointer.size()] == '/');
    });
}

unsigned InspectorBackend::GetMixedMask(std::string_view pointer, int count) const {
    unsigned mask = 0;
    for (const std::string& mixed : m_options.mixed) {
        if (mixed == pointer) {
            return (1u << count) - 1u;
        }
        for (int axis = 0; axis < count; ++axis) {
            if (mixed == std::string(pointer) + "/" + std::to_string(axis)) {
                mask |= 1u << axis;
            }
        }
    }
    return mask;
}

void InspectorBackend::MarkChanged(std::string pointer) {
    if (std::ranges::find(m_changed, pointer) == m_changed.end()) {
        m_changed.push_back(std::move(pointer));
    }
}

bool InspectorBackend::BeginRow(std::string_view name, const FieldOptions& options, std::string& pointer) {
    if (options.hidden) {
        return false;
    }
    ++m_fieldCount;
    if (m_collapsed > 0) {
        return false;
    }
    pointer = GetPointer(name);
    m_rowPointer = pointer;
    Widgets::PropertyLabel(GetLabel(name).c_str(), options.tooltip);
    ImGui::PushID(pointer.data(), pointer.data() + pointer.size());
    ImGui::BeginDisabled(options.readOnly);
    return true;
}

void InspectorBackend::EndRow() {
    m_rects.push_back({std::move(m_rowPointer), ImGui::GetItemRectMin(), ImGui::GetItemRectMax()});
    ImGui::EndDisabled();
    ImGui::PopID();
}

template <typename T>
void InspectorBackend::Number(std::string_view name, T& value, const FieldOptions& options) {
    std::string pointer;
    if (!BeginRow(name, options, pointer)) {
        return;
    }
    const bool ranged = options.min < options.max;
    const T min = ToBound<T>(options.min);
    const T max = ToBound<T>(options.max);
    // Integers use ImGui's own format for their type.
    const char* format = IsMixed(pointer) ? MixedValueText : std::is_integral_v<T> ? nullptr : "%.3f";
    if (ImGui::DragScalar("##value", GetDataType<T>(), &value, GetSpeed(options, std::is_integral_v<T>),
                          ranged ? &min : nullptr, ranged ? &max : nullptr, format, GetSliderFlags(options))) {
        MarkChanged(pointer);
    }
    EndRow();
}

void InspectorBackend::Field(std::string_view name, bool& value, const FieldOptions& options) {
    std::string pointer;
    if (!BeginRow(name, options, pointer)) {
        return;
    }
    const bool mixed = IsMixed(pointer);
    bool shown = !mixed && value;
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
    if (ImGui::Checkbox("##value", &shown)) {
        value = shown;
        MarkChanged(pointer);
    }
    ImGui::PopItemFlag();
    EndRow();
}

void InspectorBackend::Field(std::string_view name, int& value, const FieldOptions& options) {
    Number(name, value, options);
}

void InspectorBackend::Field(std::string_view name, std::uint32_t& value, const FieldOptions& options) {
    if (!options.layerMask) {
        Number(name, value, options);
        return;
    }
    std::string pointer;
    if (!BeginRow(name, options, pointer)) {
        return;
    }
    const auto layerName = [this](int layer) {
        const std::string named = m_options.project ? m_options.project->GetProject()
                                                          .GetSettings()
                                                          .sections.physics.layerNames[static_cast<std::size_t>(layer)]
                                                    : std::string();
        return named.empty() ? std::format("Layer {}", layer) : named;
    };
    int count = 0;
    for (int layer = 0; layer < Entity::LayerCount; ++layer) {
        count += (value >> layer) & 1u ? 1 : 0;
    }
    const std::string preview = IsMixed(pointer)              ? std::string(MixedValueText)
                                : count == Entity::LayerCount ? std::string("Everything")
                                : count == 0                  ? std::string("Nothing")
                                                              : std::format("{} layers", count);
    if (ImGui::BeginCombo("##value", preview.c_str())) {
        for (int layer = 0; layer < Entity::LayerCount; ++layer) {
            const std::string label = std::format("{}  {}", layer, layerName(layer));
            bool on = (value >> layer) & 1u;
            // Unnamed layers are unused, so only those already in the mask show beside the named ones.
            const bool named = m_options.project && !m_options.project->GetProject()
                                                         .GetSettings()
                                                         .sections.physics.layerNames[static_cast<std::size_t>(layer)]
                                                         .empty();
            if (!named && !on && (m_options.project || layer != 0)) {
                continue;
            }
            if (ImGui::Checkbox(label.c_str(), &on)) {
                value = on ? value | (1u << layer) : value & ~(1u << layer);
                MarkChanged(pointer);
            }
        }
        ImGui::EndCombo();
    }
    EndRow();
}

void InspectorBackend::Field(std::string_view name, std::int64_t& value, const FieldOptions& options) {
    Number(name, value, options);
}

void InspectorBackend::Field(std::string_view name, std::uint64_t& value, const FieldOptions& options) {
    Number(name, value, options);
}

void InspectorBackend::Field(std::string_view name, float& value, const FieldOptions& options) {
    if (!options.angle) {
        Number(name, value, options);
        return;
    }
    std::string pointer;
    if (!BeginRow(name, options, pointer)) {
        return;
    }
    float degrees = ToDegrees(value);
    const float speed = options.speed > 0.0f ? ToDegrees(options.speed) : 0.5f;
    if (ImGui::DragFloat("##value", &degrees, speed, ToDegrees(options.min), ToDegrees(options.max),
                         IsMixed(pointer) ? MixedValueText : DegreesFormat, GetSliderFlags(options))) {
        value = ToRadians(degrees);
        MarkChanged(pointer);
    }
    EndRow();
}

void InspectorBackend::Field(std::string_view name, double& value, const FieldOptions& options) {
    Number(name, value, options);
}

void InspectorBackend::Field(std::string_view name, std::string& value, const FieldOptions& options) {
    std::string pointer;
    if (!BeginRow(name, options, pointer)) {
        return;
    }
    if (IsMixed(pointer)) {
        std::string shown;
        if (ImGui::InputTextWithHint("##value", MixedValueText, &shown)) {
            value = shown;
            MarkChanged(pointer);
        }
    } else if (ImGui::InputText("##value", &value)) {
        MarkChanged(pointer);
    }
    EndRow();
}

template <std::size_t N>
void InspectorBackend::Floats(std::string_view name, float* values, const FieldOptions& options) {
    std::string pointer;
    if (!BeginRow(name, options, pointer)) {
        return;
    }
    const unsigned mixed = GetMixedMask(pointer, static_cast<int>(N));
    if (const auto axis = DragAxes("##value", values, static_cast<int>(N), GetSpeed(options, false), options.min,
                                   options.max, "%.3f", mixed)) {
        MarkChanged(pointer + "/" + std::to_string(*axis));
    }
    EndRow();
}

void InspectorBackend::Field(std::string_view name, Vector2& value, const FieldOptions& options) {
    std::array values{value.x, value.y};
    Floats<2>(name, values.data(), options);
    value = {values[0], values[1]};
}

void InspectorBackend::Field(std::string_view name, Vector3& value, const FieldOptions& options) {
    std::array values{value.x, value.y, value.z};
    Floats<3>(name, values.data(), options);
    value = {values[0], values[1], values[2]};
}

void InspectorBackend::Field(std::string_view name, Vector4& value, const FieldOptions& options) {
    std::array values{value.x, value.y, value.z, value.w};
    Floats<4>(name, values.data(), options);
    value = {values[0], values[1], values[2], values[3]};
}

void InspectorBackend::Field(std::string_view name, Quaternion& value, const FieldOptions& options) {
    std::string pointer;
    if (!BeginRow(name, options, pointer)) {
        return;
    }
    // Shown as Euler angles in degrees; any change replaces the whole rotation.
    const ImGuiID key = ImGui::GetID("##rotation");
    Vector3 degrees;
    if (m_options.eulers) {
        degrees = m_options.eulers->Get(key, value);
    } else {
        const Vector3 radians = ToEuler(value);
        degrees = {ToDegrees(radians.x), ToDegrees(radians.y), ToDegrees(radians.z)};
    }
    std::array angles{degrees.x, degrees.y, degrees.z};
    const unsigned mixed = IsMixed(pointer) ? 0b111u : 0u;
    if (DragAxes("##value", angles.data(), 3, options.speed > 0.0f ? ToDegrees(options.speed) : 0.5f, 0.0f, 0.0f,
                 DegreesFormat, mixed)) {
        degrees = {angles[0], angles[1], angles[2]};
        value = Quaternion::FromEuler({ToRadians(degrees.x), ToRadians(degrees.y), ToRadians(degrees.z)});
        if (m_options.eulers) {
            m_options.eulers->Set(key, degrees, value);
        }
        MarkChanged(pointer);
    }
    EndRow();
}

void InspectorBackend::Field(std::string_view name, Color& value, const FieldOptions& options) {
    std::string pointer;
    if (!BeginRow(name, options, pointer)) {
        return;
    }
    const bool mixed = IsMixed(pointer);
    std::array shown{LinearToSrgb(value.r), LinearToSrgb(value.g), LinearToSrgb(value.b), value.a};
    ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar;
    if (options.hdr) {
        flags |= ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float;
    }

    // A swatch as wide as the other fields; clicking it opens a picker.
    bool changed = false;
    const ImVec2 size{ImGui::CalcItemWidth(), ImGui::GetFrameHeight()};
    if (ImGui::ColorButton("##swatch", {shown[0], shown[1], shown[2], shown[3]}, flags, size)) {
        ImGui::OpenPopup("##picker");
    }
    if (mixed) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 textSize = ImGui::CalcTextSize(MixedValueText);
        ImGui::GetWindowDrawList()->AddText(
            {min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f},
            ImGui::GetColorU32(ImGuiCol_Text), MixedValueText);
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_3F)) {
            std::memcpy(shown.data(), payload->Data, sizeof(float) * 3);
            changed = true;
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_4F)) {
            std::memcpy(shown.data(), payload->Data, sizeof(float) * 4);
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopup("##picker")) {
        changed = ImGui::ColorPicker4("##picker", shown.data(), flags) || changed;
        ImGui::EndPopup();
    }
    if (changed) {
        value = {SrgbToLinear(shown[0]), SrgbToLinear(shown[1]), SrgbToLinear(shown[2]), shown[3]};
        MarkChanged(pointer);
    }
    EndRow();
}

void InspectorBackend::Field(std::string_view name, EntityId& value, const FieldOptions& options) {
    std::string pointer;
    if (!BeginRow(name, options, pointer)) {
        return;
    }
    Scene* scene = m_options.scene;
    const Entity* target = scene && value != EntityId::Invalid ? scene->GetEntity(value) : nullptr;
    const char* icon = target ? GetEntityIcon(*target) : ICON_FA_CIRCLE_NODES;
    std::string current = "None";
    if (!options.componentType.empty()) {
        current = std::format("None ({})", options.componentType);
    }
    const ImVec4* color = &EditorColors::TextMuted;
    if (target) {
        current = target->GetName();
        color = nullptr;
        if (options.entityFilter && !options.entityFilter(*target)) {
            current += " (wrong component type)";
            color = &EditorColors::Warning;
        }
    } else if (value != EntityId::Invalid) {
        current = "Missing entity";
        icon = ICON_FA_TRIANGLE_EXCLAMATION;
        color = &EditorColors::Warning;
    }

    std::vector<EntityId> ids;
    const PickerResult result = PickerField(
        icon, current, EntitiesPayload,
        [&] {
            std::vector<PickerItem> items;
            if (scene) {
                for (Entity* root : scene->GetRootEntities()) {
                    ListEntities(*root, {}, items, ids, options.entityFilter);
                }
            }
            return items;
        },
        IsMixed(pointer), color);
    if (result.picked) {
        value = *result.picked < 0 ? EntityId::Invalid : ids[static_cast<std::size_t>(*result.picked)];
        MarkChanged(pointer);
    } else if (!result.dropped.empty()) {
        const auto dropped = result.GetDropped<EntityId>();
        if (!dropped.empty()) {
            const Entity* droppedEntity = scene ? scene->GetEntity(dropped.front()) : nullptr;
            if (droppedEntity && (!options.entityFilter || options.entityFilter(*droppedEntity))) {
                value = dropped.front();
                MarkChanged(pointer);
            }
        }
    }
    EndRow();
}

void InspectorBackend::Field(std::string_view name, AssetId& value, std::optional<AssetType> type,
                             const FieldOptions& options) {
    std::string pointer;
    if (!BeginRow(name, options, pointer)) {
        return;
    }
    EditorProject* project = m_options.project;
    const std::optional<AssetEntry> entry = FindAsset(project, value);
    const char* icon = type ? GetAssetTypeIcon(*type) : ICON_FA_FILE;
    std::string current = "None";
    const ImVec4* color = &EditorColors::TextMuted;
    if (entry) {
        current = entry->name;
        color = nullptr;
    } else if (value.IsValid()) {
        current = "Missing asset";
        icon = ICON_FA_TRIANGLE_EXCLAMATION;
        color = &EditorColors::Warning;
    }

    std::vector<AssetId> ids;
    const PickerResult result = PickerField(
        icon, current, AssetsPayload,
        [&] {
            std::vector<PickerItem> items;
            for (const AssetEntry& listed : ListAssets(project, type)) {
                items.push_back({listed.name, GetAssetTypeIcon(listed.type), GetAssetDetail(listed)});
                ids.push_back(listed.id);
            }
            return items;
        },
        IsMixed(pointer), color, entry ? entry->path.c_str() : nullptr);
    if (result.picked) {
        value = *result.picked < 0 ? AssetId() : ids[static_cast<std::size_t>(*result.picked)];
        MarkChanged(pointer);
    } else if (!result.dropped.empty()) {
        // Only an asset of the field's type can go in it.
        for (const AssetId dropped : result.GetDropped<AssetId>()) {
            const std::optional<AssetEntry> droppedEntry = FindAsset(project, dropped);
            if (droppedEntry && (!type || droppedEntry->type == *type)) {
                value = dropped;
                MarkChanged(pointer);
                break;
            }
        }
    }
    EndRow();
}

void InspectorBackend::EnumField(std::string_view name, std::int64_t& value, const FieldOptions& options) {
    const std::span<const std::string_view> names = options.enumNames;
    if (names.empty()) {
        Number(name, value, options);
        return;
    }
    std::string pointer;
    if (!BeginRow(name, options, pointer)) {
        return;
    }
    const bool known = value >= 0 && static_cast<std::size_t>(value) < names.size();
    const std::string preview = IsMixed(pointer) ? MixedValueText
                                : known          ? MakeDisplayName(names[static_cast<std::size_t>(value)])
                                                 : std::to_string(value);
    if (ImGui::BeginCombo("##value", preview.c_str())) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            const bool selected = static_cast<std::int64_t>(i) == value;
            if (ImGui::Selectable(MakeDisplayName(names[i]).c_str(), selected) && !selected) {
                value = static_cast<std::int64_t>(i);
                MarkChanged(pointer);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    EndRow();
}

void InspectorBackend::CurveField(std::string_view name, Curve& value, const FieldOptions& options) {
    std::string pointer;
    if (BeginRow(name, options, pointer)) {
        std::array<float, 64> samples{};
        const auto keys = value.GetKeys();
        const float start = keys.empty() ? 0 : keys.front().time;
        const float end = keys.empty() ? 1 : keys.back().time;
        for (std::size_t i = 0; i < samples.size(); ++i)
            samples[i] = value.Evaluate(Lerp(start, end, static_cast<float>(i) / 63));
        ImGui::PlotLines("##curve", samples.data(), static_cast<int>(samples.size()), 0, nullptr, FLT_MAX, FLT_MAX,
                         {0, 48});
        EndRow();
    }
    ArchiveBackend::CurveField(name, value, options);
}
void InspectorBackend::GradientField(std::string_view name, Gradient& value, const FieldOptions& options) {
    std::string pointer;
    if (BeginRow(name, options, pointer)) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 size{ImGui::GetContentRegionAvail().x, 24};
        auto* draw = ImGui::GetWindowDrawList();
        const auto keys = value.GetKeys();
        const float start = keys.empty() ? 0 : keys.front().time;
        const float end = keys.empty() ? 1 : keys.back().time;
        for (int i = 0; i < 32; ++i) {
            const auto c = value.Evaluate(Lerp(start, end, static_cast<float>(i) / 31));
            const auto color =
                ImGui::ColorConvertFloat4ToU32({LinearToSrgb(c.r), LinearToSrgb(c.g), LinearToSrgb(c.b), c.a});
            draw->AddRectFilled({at.x + size.x * static_cast<float>(i) / 32, at.y},
                                {at.x + size.x * static_cast<float>(i + 1) / 32, at.y + size.y}, color);
        }
        ImGui::Dummy(size);
        EndRow();
    }
    ArchiveBackend::GradientField(name, value, options);
}

bool InspectorBackend::BeginContainer(std::string_view name, const FieldOptions& options, std::size_t* size) {
    // Not opened, so the archive skips what is inside.
    if (options.hidden) {
        return false;
    }
    ++m_fieldCount;
    const std::string pointer = GetPointer(name);
    bool open = false;
    if (m_collapsed == 0) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        // Objects start open, as they usually hold a few fields; arrays start closed, as they can be long.
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
        if (!size) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        open = ImGui::TreeNodeEx(pointer.c_str(), flags, "%s", GetLabel(name).c_str());
        if (options.tooltip) {
            ImGui::SetItemTooltip("%s", options.tooltip);
        }
        if (size) {
            ImGui::TableNextColumn();
            ImGui::PushID(pointer.data(), pointer.data() + pointer.size());
            const float start = ImGui::GetCursorPosX();
            const float available = ImGui::GetContentRegionAvail().x;
            ImGui::AlignTextToFramePadding();
            if (IsMixed(pointer)) {
                ImGui::TextUnformatted(MixedValueText);
            } else {
                ImGui::TextDisabled("%zu %s", *size, *size == 1 ? "item" : "items");
            }
            if (!options.readOnly) {
                const float button = ImGui::GetFrameHeight();
                const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
                ImGui::SameLine(start + available - button * 2.0f - spacing);
                if (ImGui::Button(ICON_FA_PLUS, {button, button})) {
                    ++*size;
                    MarkChanged(pointer);
                }
                ImGui::SetItemTooltip("Add an element at the end");
                ImGui::SameLine(0.0f, spacing);
                ImGui::BeginDisabled(*size == 0);
                if (ImGui::Button(ICON_FA_MINUS, {button, button})) {
                    --*size;
                    MarkChanged(pointer);
                }
                ImGui::SetItemTooltip("Remove the last element");
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
    }
    m_containers.push_back({m_path, open});
    if (!open) {
        ++m_collapsed;
    }
    m_path = pointer;
    return true;
}

void InspectorBackend::EndContainer() {
    const Container container = std::move(m_containers.back());
    m_containers.pop_back();
    if (container.open) {
        ImGui::TreePop();
    } else {
        --m_collapsed;
    }
    m_path = container.path;
}

bool InspectorBackend::BeginObject(std::string_view name, const FieldOptions& options) {
    return BeginContainer(name, options, nullptr);
}

void InspectorBackend::EndObject() {
    EndContainer();
}

bool InspectorBackend::BeginArray(std::string_view name, std::size_t& size, const FieldOptions& options) {
    return BeginContainer(name, options, &size);
}

void InspectorBackend::BeginElement(std::size_t index) {
    m_elementPaths.push_back(m_path);
    m_path += "/" + std::to_string(index);
    m_element = index;
}

void InspectorBackend::EndElement() {
    m_path = std::move(m_elementPaths.back());
    m_elementPaths.pop_back();
    m_element.reset();
}

void InspectorBackend::EndArray() {
    EndContainer();
}

AssetManager* InspectorBackend::GetAssetManager() const {
    return m_options.project ? &m_options.project->GetAssets() : nullptr;
}

} // namespace Spot::Editor
