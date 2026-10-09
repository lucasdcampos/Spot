#include "inspector/entity_inspector.h"

#include "core/text.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/widgets.h"
#include "inspector/inspector_backend.h"
#include "inspector/inspector_clipboard.h"
#include "inspector/multi_edit.h"
#include "scene/entity_actions.h"
#include "scene/scene_workspace.h"

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <set>
#include <unordered_map>

namespace Spot::Editor {

namespace {

constexpr const char* DegreesFormat = "%.1f\xC2\xB0";

float& At(Vector3& vector, int axis) {
    return axis == 0 ? vector.x : axis == 1 ? vector.y : vector.z;
}

float At(const Vector3& vector, int axis) {
    return axis == 0 ? vector.x : axis == 1 ? vector.y : vector.z;
}

std::array<float, 3> ToArray(const Vector3& vector) {
    return {vector.x, vector.y, vector.z};
}

std::uint64_t GetEulerKey(const Entity& entity) {
    return static_cast<std::uint64_t>(entity.GetId());
}

// A bit for each axis on which some entity's value is not the primary entity's, which comes first.
template <typename Get>
unsigned GetMixedAxes(std::span<Entity* const> entities, Get get, float tolerance = 0.0f) {
    const Vector3 primary = get(*entities.front());
    unsigned mask = 0;
    for (const Entity* entity : entities.subspan(1)) {
        const Vector3 value = get(*entity);
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(At(value, axis) - At(primary, axis)) > tolerance) {
                mask |= 1u << axis;
            }
        }
    }
    return mask;
}

std::vector<EntityId> GetIds(std::span<Entity* const> entities) {
    std::vector<EntityId> ids;
    for (const Entity* entity : entities) {
        ids.push_back(entity->GetId());
    }
    return ids;
}

// The place of the entity's component of the type that comes after as many others of that type.
std::optional<std::size_t> FindComponent(const Entity& entity, std::string_view typeName, std::size_t occurrence) {
    const std::vector<Component*> components = entity.GetComponents<Component>();
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (GetComponentTypeName(*components[i]) == typeName && occurrence-- == 0) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace

EntityInspector::EntityInspector(EditorContext& context) : m_context(context) {
}

std::optional<std::pair<ImVec2, ImVec2>> EntityInspector::GetItemRect(std::string_view item) const {
    const auto found = m_rects.find(std::string(item));
    return found != m_rects.end() ? std::optional(found->second) : std::nullopt;
}

std::vector<std::string> EntityInspector::GetMixedValues(const SceneDocument& document, std::string_view card) const {
    const auto found = m_mixed.find(std::format("{}/{}", document.GetId(), card));
    return found != m_mixed.end() ? found->second.pointers : std::vector<std::string>();
}

void EntityInspector::Record(std::string item) {
    m_rects[std::move(item)] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
}

bool EntityInspector::MenuItem(const std::string& key, const char* icon, const char* label, bool enabled) {
    const bool clicked = ImGui::MenuItem((std::string(icon) + "  " + label).c_str(), nullptr, false, enabled);
    Record(key + "/menu/" + label);
    return clicked;
}

void EntityInspector::Draw(SceneDocument& document) {
    Scene& scene = document.GetScene();
    Entity* primary = scene.GetEntity(document.GetSelection().GetPrimary());
    if (!primary) {
        return;
    }
    m_rects.clear();
    std::vector<Entity*> entities{primary};
    for (const EntityId id : document.GetSelection().GetEntities()) {
        Entity* entity = scene.GetEntity(id);
        if (entity && entity != primary) {
            entities.push_back(entity);
        }
    }

    DrawHeader(document, entities);
    // The header stays in view while the components scroll.
    if (ImGui::BeginChild("##body", {0.0f, 0.0f}, ImGuiChildFlags_None)) {
        DrawTransform(document, entities);
        DrawComponents(document, entities);
        DrawAddComponent(document, entities);
    }
    ImGui::EndChild();

    if (m_afterDraw) {
        std::exchange(m_afterDraw, {})();
    }
}

void EntityInspector::FinishEdit() {
    if (!m_edit) {
        return;
    }
    const ImGuiID active = ImGui::GetActiveID();
    if (active == 0 || active != m_edit->widget) {
        CommitEdit();
    }
}

void EntityInspector::DrawHeader(SceneDocument& document, std::span<Entity* const> entities) {
    Entity& primary = *entities.front();
    const bool multiple = entities.size() > 1;

    const bool activeMixed = std::ranges::any_of(
        entities, [&](const Entity* entity) { return entity->IsActiveSelf() != primary.IsActiveSelf(); });
    bool active = primary.IsActiveSelf() && !activeMixed;
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, activeMixed);
    if (ImGui::Checkbox("##active", &active)) {
        document.Execute(std::make_unique<SetEntitiesActiveCommand>(document, GetIds(entities), active));
    }
    Record("Active");
    ImGui::PopItemFlag();
    ImGui::SetItemTooltip("Whether the entity, and the entities below it, are active");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(multiple ? ICON_FA_LAYER_GROUP : GetEntityIcon(primary));
    ImGui::SameLine();

    // The name is applied once it is typed, as one step.
    const bool namesMixed =
        std::ranges::any_of(entities, [&](const Entity* entity) { return entity->GetName() != primary.GetName(); });
    if (ImGui::GetActiveID() != ImGui::GetID("##name")) {
        m_name = namesMixed ? std::string() : primary.GetName();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##name", namesMixed ? MixedValueText : "Name", &m_name);
    Record("Name");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::vector<std::unique_ptr<SceneCommand>> commands;
        for (const Entity* entity : entities) {
            commands.push_back(std::make_unique<RenameEntityCommand>(document, entity->GetId(), m_name));
        }
        Execute(document, multiple ? "Rename Entities" : "Rename Entity", std::move(commands));
    }

    DrawTags(document, entities);
    DrawLayer(document, entities);
    if (multiple) {
        ImGui::TextDisabled("%zu entities selected", entities.size());
    }
    ImGui::Separator();
}

void EntityInspector::DrawLayer(SceneDocument& document, std::span<Entity* const> entities) {
    const Entity& primary = *entities.front();
    const bool mixed =
        std::ranges::any_of(entities, [&](const Entity* entity) { return entity->GetLayer() != primary.GetLayer(); });
    const EditorProject* project = m_context.GetProject();
    const auto name = [project](int layer) {
        const std::string& named =
            project ? project->GetProject().GetSettings().sections.physics.layerNames[static_cast<std::size_t>(layer)]
                    : std::string();
        return named.empty() ? std::format("Layer {}", layer) : named;
    };

    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Layer");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string preview = mixed ? MixedValueText : name(primary.GetLayer());
    if (ImGui::BeginCombo("##layer", preview.c_str())) {
        for (int layer = 0; layer < Entity::LayerCount; ++layer) {
            const bool current = !mixed && layer == primary.GetLayer();
            // Unnamed layers are unused, so only the one the entity is on shows.
            const bool named = project && !project->GetProject()
                                               .GetSettings()
                                               .sections.physics.layerNames[static_cast<std::size_t>(layer)]
                                               .empty();
            if (!named && !current && (project || layer != 0)) {
                continue;
            }
            if (ImGui::Selectable(std::format("{}  {}", layer, name(layer)).c_str(), current)) {
                document.Execute(std::make_unique<SetEntitiesLayerCommand>(document, GetIds(entities), layer));
            }
        }
        ImGui::EndCombo();
    }
    Record("Layer");
    ImGui::SetItemTooltip("Decides what the entity's colliders collide with, and what physics queries see. Layers "
                          "are named in the project settings.");
}

void EntityInspector::DrawTags(SceneDocument& document, std::span<Entity* const> entities) {
    // The tags every selected entity has.
    std::vector<std::string> tags;
    for (const std::string& tag : entities.front()->GetTags()) {
        if (std::ranges::all_of(entities, [&](const Entity* entity) { return entity->HasTag(tag); })) {
            tags.push_back(tag);
        }
    }
    const auto change = [&](const std::string& tag, bool add) {
        document.Execute(std::make_unique<SetEntitiesTagCommand>(document, GetIds(entities), tag, add));
    };

    const ImGuiStyle& style = ImGui::GetStyle();
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(EditorColors::TextMuted, ICON_FA_TAG);
    ImGui::SetItemTooltip("Tags group entities, so game code can find them");
    const auto placeNext = [&](const char* label) {
        const float width = ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2.0f;
        if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right) {
            ImGui::SameLine();
        }
    };
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFontSize());
    for (const std::string& tag : tags) {
        const std::string label = tag + "  " ICON_FA_XMARK "###" + tag;
        placeNext(label.c_str());
        if (ImGui::SmallButton(label.c_str())) {
            change(tag, false);
        }
        ImGui::SetItemTooltip("Remove the tag");
    }
    placeNext(ICON_FA_PLUS " Tag");
    if (ImGui::SmallButton(ICON_FA_PLUS " Tag")) {
        m_newTag.clear();
        ImGui::OpenPopup("##add-tag");
    }
    Record("AddTag");
    ImGui::PopStyleVar();

    if (ImGui::BeginPopup("##add-tag")) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(220.0f * style.FontScaleDpi);
        if (ImGui::InputTextWithHint("##tag", "New tag", &m_newTag, ImGuiInputTextFlags_EnterReturnsTrue) &&
            !m_newTag.empty()) {
            change(m_newTag, true);
            ImGui::CloseCurrentPopup();
        }
        // Tags already used in the scene, to pick without typing them.
        std::set<std::string> used;
        document.GetScene().ForEachEntity([&](const Entity& entity) {
            for (const std::string& tag : entity.GetTags()) {
                if (std::ranges::find(tags, tag) == tags.end() && ContainsIgnoringCase(tag, m_newTag)) {
                    used.insert(tag);
                }
            }
        });
        if (!used.empty()) {
            ImGui::SeparatorText("In this scene");
            for (const std::string& tag : used) {
                if (ImGui::Selectable(tag.c_str())) {
                    change(tag, true);
                }
            }
        }
        ImGui::EndPopup();
    }
}

EntityInspector::CardHeader EntityInspector::DrawCardHeader(const std::string& key, const char* icon,
                                                            const ImVec4& iconColor, const std::string& title,
                                                            std::optional<bool> enabled, bool enabledMixed) {
    CardHeader result;
    const ImGuiStyle& style = ImGui::GetStyle();
    result.open = ImGui::CollapsingHeader("##card", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::OpenPopupOnItemClick("##menu", ImGuiPopupFlags_MouseButtonRight);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 next = ImGui::GetCursorScreenPos();
    const float height = max.y - min.y;

    // After the arrow, where the header's label would be.
    float x = min.x + ImGui::GetFontSize() + style.FramePadding.x * 3.0f;
    if (enabled) {
        // Smaller than the header, and centered in it.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            {style.FramePadding.x, std::floor(style.FramePadding.y * 0.5f)});
        const float size = ImGui::GetFrameHeight();
        ImGui::SetCursorScreenPos({x, min.y + std::floor((height - size) * 0.5f)});
        bool value = *enabled && !enabledMixed;
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, enabledMixed);
        result.toggled = ImGui::Checkbox("##enabled", &value);
        Record(key + "/enabled");
        ImGui::PopItemFlag();
        ImGui::PopStyleVar();
        ImGui::SetItemTooltip("Enabled");
        x += size + style.ItemInnerSpacing.x * 2.0f;
    }

    const float buttonWidth = ImGui::GetFontSize() + style.FramePadding.x * 2.0f;
    const float buttonX = max.x - buttonWidth - style.FramePadding.x * 0.5f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float textY = min.y + (height - ImGui::GetFontSize()) * 0.5f;
    drawList->PushClipRect({x, min.y}, {buttonX, max.y}, true);
    drawList->AddText({x, textY}, ImGui::GetColorU32(iconColor), icon);
    x += ImGui::CalcTextSize(icon).x + style.ItemInnerSpacing.x * 2.0f;
    drawList->AddText(GetBoldFont(), ImGui::GetFontSize(), {x, textY}, ImGui::GetColorU32(ImGuiCol_Text),
                      title.c_str());
    drawList->PopClipRect();

    ImGui::SetCursorScreenPos({buttonX, min.y});
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::Button(ICON_FA_ELLIPSIS_VERTICAL "##menu-button", {buttonWidth, height})) {
        ImGui::OpenPopup("##menu");
    }
    Record(key + "/menu");
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(next);
    return result;
}

void EntityInspector::DrawTransform(SceneDocument& document, std::span<Entity* const> entities) {
    ImGui::PushID("Transform");
    const CardHeader header = DrawCardHeader("Transform", ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, EditorColors::Accent,
                                             "Transform", std::nullopt, false);
    if (ImGui::BeginPopup("##menu")) {
        DrawTransformMenu(document, entities);
        ImGui::EndPopup();
    }
    if (!header.open || !Widgets::BeginProperties("##fields")) {
        ImGui::PopID();
        return;
    }
    Entity& primary = *entities.front();
    Transform& transform = primary.GetTransform();

    Widgets::PropertyLabel("Position");
    std::array position = ToArray(transform.GetPosition());
    const unsigned mixedPosition =
        GetMixedAxes(entities, [](const Entity& entity) { return entity.GetTransform().GetPosition(); });
    const auto moved = DragAxes("##position", position.data(), 3, 0.01f, 0.0f, 0.0f, "%.3f", mixedPosition);
    Record("Transform/position");
    if (const auto axis = moved) {
        BeginTransformEdit(document, entities, "Move");
        for (Entity* entity : entities) {
            Vector3 value = entity->GetTransform().GetPosition();
            At(value, *axis) = position[static_cast<std::size_t>(*axis)];
            entity->GetTransform().SetPosition(value);
        }
    }

    // Rotations show as the Euler angles they were typed as.
    const auto getDegrees = [&](const Entity& entity) {
        return m_transformEulers.Get(GetEulerKey(entity), entity.GetTransform().GetRotation());
    };
    Widgets::PropertyLabel("Rotation");
    std::array rotation = ToArray(getDegrees(primary));
    const unsigned mixedRotation = GetMixedAxes(entities, getDegrees, 0.01f);
    const auto rotated = DragAxes("##rotation", rotation.data(), 3, 0.5f, 0.0f, 0.0f, DegreesFormat, mixedRotation);
    Record("Transform/rotation");
    if (const auto axis = rotated) {
        BeginTransformEdit(document, entities, "Rotate");
        for (Entity* entity : entities) {
            Vector3 degrees = getDegrees(*entity);
            At(degrees, *axis) = rotation[static_cast<std::size_t>(*axis)];
            const Quaternion value =
                Quaternion::FromEuler({ToRadians(degrees.x), ToRadians(degrees.y), ToRadians(degrees.z)});
            entity->GetTransform().SetRotation(value);
            m_transformEulers.Set(GetEulerKey(*entity), degrees, value);
        }
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Scale");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, m_uniformScale ? EditorColors::Accent : EditorColors::TextMuted);
    if (ImGui::SmallButton(m_uniformScale ? ICON_FA_LINK : ICON_FA_LINK_SLASH)) {
        m_uniformScale = !m_uniformScale;
    }
    ImGui::PopStyleColor(2);
    ImGui::SetItemTooltip(m_uniformScale ? "Scaling keeps the proportions" : "Each axis scales on its own");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    std::array scale = ToArray(transform.GetScale());
    const unsigned mixedScale =
        GetMixedAxes(entities, [](const Entity& entity) { return entity.GetTransform().GetScale(); });
    const auto scaled = DragAxes("##scale", scale.data(), 3, 0.01f, 0.0f, 0.0f, "%.3f", mixedScale);
    Record("Transform/scale");
    if (const auto axis = scaled) {
        BeginTransformEdit(document, entities, "Scale");
        const float typed = scale[static_cast<std::size_t>(*axis)];
        for (Entity* entity : entities) {
            Vector3 value = entity->GetTransform().GetScale();
            const float previous = At(value, *axis);
            if (!m_uniformScale) {
                At(value, *axis) = typed;
            } else if (previous != 0.0f) {
                value = value * (typed / previous);
            } else {
                value = {typed, typed, typed};
            }
            entity->GetTransform().SetScale(value);
        }
    }
    Widgets::EndProperties();
    ImGui::PopID();
}

void EntityInspector::DrawTransformMenu(SceneDocument& document, std::span<Entity* const> entities) {
    const auto setAll = [&](const char* name, const Pose& pose) {
        std::vector<SetTransformsCommand::Change> changes;
        for (const Entity* entity : entities) {
            changes.push_back({entity->GetId(), Pose::Of(entity->GetTransform()), pose});
        }
        document.Execute(std::make_unique<SetTransformsCommand>(document, name, std::move(changes)));
    };
    if (MenuItem("Transform", ICON_FA_ROTATE_LEFT, "Reset")) {
        setAll("Reset Transform", Pose{});
    }
    ImGui::Separator();
    if (MenuItem("Transform", ICON_FA_COPY, "Copy")) {
        Clipboard::SetText(MakeTransformClipboardText(Pose::Of(entities.front()->GetTransform())));
    }
    const std::optional<Pose> copied = ReadTransformClipboardText(Clipboard::GetText());
    if (MenuItem("Transform", ICON_FA_PASTE, "Paste", copied.has_value())) {
        setAll("Paste Transform", *copied);
    }
}

void EntityInspector::DrawComponents(SceneDocument& document, std::span<Entity* const> entities) {
    Entity& primary = *entities.front();
    const std::vector<Component*> components = primary.GetComponents<Component>();
    std::unordered_map<std::string, std::size_t> seen;
    std::size_t hidden = 0;
    for (std::size_t i = 0; i < components.size(); ++i) {
        Card card{GetComponentTypeName(*components[i]), {}, {{primary.GetId(), i}}};
        const std::size_t occurrence = seen[card.typeName]++;
        card.key = occurrence == 0 ? card.typeName : card.typeName + "#" + std::to_string(occurrence);
        // Several entities show the components they all have.
        bool shared = true;
        for (const Entity* entity : entities.subspan(1)) {
            const std::optional<std::size_t> index = FindComponent(*entity, card.typeName, occurrence);
            if (!index) {
                shared = false;
                break;
            }
            card.components.emplace_back(entity->GetId(), *index);
        }
        if (!shared) {
            ++hidden;
            continue;
        }
        // Cards keep their state by type, so collapsing one collapses it on every entity.
        ImGui::PushID(card.typeName.c_str());
        ImGui::PushID(static_cast<int>(occurrence));
        DrawComponent(document, card);
        ImGui::PopID();
        ImGui::PopID();
    }
    if (hidden > 0) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(EditorColors::TextMuted, "%zu %s not shown, as not every selected entity has %s.", hidden,
                           hidden == 1 ? "component is" : "components are", hidden == 1 ? "it" : "them");
        ImGui::PopTextWrapPos();
    }
}

void EntityInspector::DrawComponent(SceneDocument& document, const Card& card) {
    Scene& scene = document.GetScene();
    std::vector<Component*> components;
    for (const auto& [id, index] : card.components) {
        components.push_back(&GetComponentAt(*scene.GetEntity(id), index));
    }
    Component& component = *components.front();
    const bool missing = dynamic_cast<const MissingComponent*>(&component) != nullptr;

    // Saved before drawing, so an edit knows how the component was before it.
    std::string text = SaveComponentToString(component);
    const std::vector<std::string> mixed = GetMixedValues(document, card, components, text);
    const bool enabledMixed = std::ranges::find(mixed, "/enabled") != mixed.end();

    const bool faulted = component.IsFaulted();
    const std::string title = missing   ? card.typeName + " (missing)"
                              : faulted ? MakeDisplayName(card.typeName) + " (faulted)"
                                        : MakeDisplayName(card.typeName);
    const char* icon = missing || faulted ? ICON_FA_TRIANGLE_EXCLAMATION : GetComponentIcon(card.typeName);
    const ImVec4& iconColor = missing ? EditorColors::Warning : faulted ? EditorColors::Error : EditorColors::Accent;
    const CardHeader header = DrawCardHeader(
        card.key, icon, iconColor, title, missing ? std::nullopt : std::optional(component.IsEnabled()), enabledMixed);
    if (header.toggled) {
        const bool enabled = enabledMixed || !component.IsEnabled();
        std::vector<std::unique_ptr<SceneCommand>> commands;
        for (std::size_t i = 0; i < components.size(); ++i) {
            const std::string before = i == 0 ? text : SaveComponentToString(*components[i]);
            components[i]->SetEnabled(enabled);
            commands.push_back(std::make_unique<EditComponentCommand>(document, card.components[i].first,
                                                                      card.components[i].second, before,
                                                                      SaveComponentToString(*components[i])));
        }
        Execute(document, (enabled ? "Enable " : "Disable ") + MakeDisplayName(card.typeName), std::move(commands));
        text = SaveComponentToString(component);
    }
    if (ImGui::BeginPopup("##menu")) {
        DrawComponentMenu(document, card, text);
        ImGui::EndPopup();
    }
    if (!header.open) {
        return;
    }
    if (missing) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(EditorColors::TextMuted,
                           "Its type is not registered, as when the game code that defines it is not loaded. Its "
                           "values are kept, and saved with the scene.");
        ImGui::PopTextWrapPos();
        return;
    }
    if (faulted) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(EditorColors::Error,
                           "One of its hooks threw, so it stopped running until the scene is reloaded. The console "
                           "says where.");
        ImGui::PopTextWrapPos();
    }
    if (!Widgets::BeginProperties("##fields")) {
        return;
    }
    InspectorBackend backend({
        .scene = &scene,
        .project = m_context.GetProject(),
        .eulers = &m_fieldEulers,
        .mixed = mixed,
    });
    Archive archive(backend);
    SerializeComponent(component, archive);
    Widgets::EndProperties();
    for (const InspectorBackend::FieldRect& rect : backend.GetFieldRects()) {
        m_rects[card.key + rect.pointer] = {rect.min, rect.max};
    }
    if (backend.GetFieldCount() == 0) {
        ImGui::TextDisabled("No properties");
    }
    if (!backend.WasChanged()) {
        return;
    }

    // The others are as they were until they get the values that changed, below.
    std::vector<std::string> before;
    const bool continuing = m_edit && m_edit->document == document.GetId() && m_edit->components == card.components;
    if (!continuing) {
        before.push_back(text);
        for (std::size_t i = 1; i < components.size(); ++i) {
            before.push_back(SaveComponentToString(*components[i]));
        }
    }
    BeginComponentEdit(document, card, std::move(before));
    ValidateComponent(component);
    for (const std::string& pointer : backend.GetChanged()) {
        if (std::ranges::find(m_edit->changed, pointer) == m_edit->changed.end()) {
            m_edit->changed.push_back(pointer);
        }
    }
    // The other entities get the values that changed, and keep their own others.
    if (components.size() > 1) {
        const std::string edited = SaveComponentToString(component);
        for (std::size_t i = 1; i < components.size(); ++i) {
            LoadComponentFromString(*components[i],
                                    CopyValues(edited, SaveComponentToString(*components[i]), m_edit->changed));
        }
    }
}

const std::vector<std::string>& EntityInspector::GetMixedValues(SceneDocument& document, const Card& card,
                                                                std::span<Component* const> components,
                                                                const std::string& text) {
    MixedValues& cached = m_mixed[std::format("{}/{}", document.GetId(), card.key)];
    // Values change without a command only while being edited here, or while a game plays.
    const bool editing = m_edit && m_edit->components == card.components;
    const bool playing = m_context.GetWorkspace() && m_context.GetWorkspace()->GetPlaySession();
    constexpr double PlayingRefresh = 0.25;
    const double now = ImGui::GetTime();
    const bool current = cached.selection == document.GetSelection().GetVersion() &&
                         cached.history == document.GetHistory().GetCurrentState() &&
                         cached.components == components.size() && !editing &&
                         (!playing || now - cached.time < PlayingRefresh);
    if (current && cached.time > 0.0) {
        return cached.pointers;
    }
    std::vector<std::string> others;
    for (std::size_t i = 1; i < components.size(); ++i) {
        others.push_back(SaveComponentToString(*components[i]));
    }
    cached = {.selection = document.GetSelection().GetVersion(),
              .history = document.GetHistory().GetCurrentState(),
              .components = components.size(),
              .time = now,
              .pointers = FindMixedValues(text, others)};
    return cached.pointers;
}

void EntityInspector::DrawComponentMenu(SceneDocument& document, const Card& card, const std::string& text) {
    Scene& scene = document.GetScene();
    const EntityId primaryId = card.components.front().first;
    const std::size_t index = card.components.front().second;
    const bool multiple = card.components.size() > 1;
    const bool missing = dynamic_cast<const MissingComponent*>(&GetComponentAt(*scene.GetEntity(primaryId), index));
    const std::size_t count = scene.GetEntity(primaryId)->GetComponents<Component>().size();
    const std::string displayName = MakeDisplayName(card.typeName);

    // Runs after the cards are drawn: each command makes one per selected entity, as one step.
    const auto forEach = [&](std::string name, auto make) {
        m_afterDraw = [this, &document, card, name = std::move(name), make] {
            std::vector<std::unique_ptr<SceneCommand>> commands;
            for (const auto& [id, place] : card.components) {
                if (auto command = make(id, place)) {
                    commands.push_back(std::move(command));
                }
            }
            Execute(document, name, std::move(commands));
        };
    };
    const auto edit = [&document, &scene](EntityId id, std::size_t place, const auto& change) {
        const std::string before = SaveComponentToString(GetComponentAt(*scene.GetEntity(id), place));
        return std::make_unique<EditComponentCommand>(document, id, place, before, change(before));
    };

    if (MenuItem(card.key, ICON_FA_ROTATE_LEFT, "Reset", !missing)) {
        forEach("Reset " + displayName, [edit, typeName = card.typeName](EntityId id, std::size_t place) {
            return edit(id, place, [&](const std::string& before) {
                // Back to the defaults, without changing whether it is enabled.
                const std::unique_ptr<Component> defaults = CreateComponent(typeName);
                const std::array enabled{std::string("/enabled")};
                return defaults ? CopyValues(before, SaveComponentToString(*defaults), enabled) : before;
            });
        });
    }
    ImGui::Separator();
    if (MenuItem(card.key, ICON_FA_ARROW_UP, "Move Up", !multiple && index > 0)) {
        m_afterDraw = [&document, id = primaryId, index] {
            document.Execute(std::make_unique<MoveComponentCommand>(document, id, index, index - 1));
        };
    }
    if (MenuItem(card.key, ICON_FA_ARROW_DOWN, "Move Down", !multiple && index + 1 < count)) {
        m_afterDraw = [&document, id = primaryId, index] {
            document.Execute(std::make_unique<MoveComponentCommand>(document, id, index, index + 1));
        };
    }
    ImGui::Separator();
    if (MenuItem(card.key, ICON_FA_COPY, "Copy")) {
        Clipboard::SetText(MakeComponentClipboardText(text));
    }
    const std::optional<CopiedComponent> copied = ReadComponentClipboardText(Clipboard::GetText());
    if (MenuItem(card.key, ICON_FA_PASTE, "Paste Values", !missing && copied && copied->typeName == card.typeName)) {
        forEach("Paste Values", [edit, copiedText = copied->text](EntityId id, std::size_t place) {
            return edit(id, place, [&](const std::string& before) {
                const std::array data{std::string("/data")};
                return CopyValues(copiedText, before, data);
            });
        });
    }
    if (MenuItem(card.key, ICON_FA_PASTE, "Paste as New", copied.has_value())) {
        forEach("Paste Component", [&document, copiedComponent = *copied](EntityId id, std::size_t place) {
            return std::make_unique<AddComponentCommand>(document, id, copiedComponent.typeName, copiedComponent.text,
                                                         place + 1);
        });
    }
    ImGui::Separator();
    if (MenuItem(card.key, ICON_FA_TRASH_CAN, "Remove")) {
        forEach("Remove " + displayName, [&document](EntityId id, std::size_t place) {
            return std::make_unique<RemoveComponentCommand>(document, id, place);
        });
    }
}

void EntityInspector::DrawAddComponent(SceneDocument& document, std::span<Entity* const> entities) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::Dummy({0.0f, style.ItemSpacing.y});
    const float available = ImGui::GetContentRegionAvail().x;
    const float width = std::min(available, 280.0f * style.FontScaleDpi);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((available - width) * 0.5f));
    if (ImGui::Button(ICON_FA_PLUS "  Add Component", {width, 0.0f})) {
        m_componentSearch.clear();
        m_componentHighlight = 0;
        ImGui::OpenPopup("##add-component");
    }
    Record("AddComponent");
    ImGui::SetNextWindowPos({ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y}, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize({width, 0.0f});
    if (!ImGui::BeginPopup("##add-component")) {
        return;
    }
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool entered = ImGui::InputTextWithHint("##search", ICON_FA_MAGNIFYING_GLASS "  Search", &m_componentSearch,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    std::vector<std::string> matches;
    for (std::string& name : GetComponentTypeNames()) {
        if (ContainsIgnoringCase(MakeDisplayName(name), m_componentSearch) ||
            ContainsIgnoringCase(name, m_componentSearch)) {
            matches.push_back(std::move(name));
        }
    }
    bool moved = false;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && m_componentHighlight + 1 < matches.size()) {
        ++m_componentHighlight;
        moved = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && m_componentHighlight > 0) {
        --m_componentHighlight;
        moved = true;
    }
    m_componentHighlight = std::min(m_componentHighlight, matches.empty() ? 0 : matches.size() - 1);

    std::optional<std::string> chosen;
    const float rows = static_cast<float>(std::clamp<std::size_t>(matches.size(), 1, 10));
    if (ImGui::BeginChild("##types", {0.0f, rows * ImGui::GetTextLineHeightWithSpacing()})) {
        for (std::size_t i = 0; i < matches.size(); ++i) {
            const std::string label =
                std::string(GetComponentIcon(matches[i])) + "  " + MakeDisplayName(matches[i]) + "###" + matches[i];
            if (ImGui::Selectable(label.c_str(), i == m_componentHighlight)) {
                chosen = matches[i];
            }
            if (moved && i == m_componentHighlight) {
                ImGui::SetScrollHereY();
            }
        }
        if (matches.empty()) {
            ImGui::TextDisabled("No component matches");
        }
    }
    ImGui::EndChild();
    if (entered && !matches.empty()) {
        chosen = matches[m_componentHighlight];
    }
    if (chosen) {
        std::vector<std::unique_ptr<SceneCommand>> commands;
        for (const Entity* entity : entities) {
            commands.push_back(std::make_unique<AddComponentCommand>(document, entity->GetId(), *chosen));
        }
        Execute(document, "Add " + MakeDisplayName(*chosen), std::move(commands));
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EntityInspector::BeginTransformEdit(SceneDocument& document, std::span<Entity* const> entities, const char* name) {
    const bool same =
        m_edit && m_edit->document == document.GetId() && m_edit->name == name &&
        m_edit->transforms.size() == entities.size() &&
        std::ranges::equal(m_edit->transforms, entities, {}, &SetTransformsCommand::Change::entity, &Entity::GetId);
    if (m_edit && !same) {
        CommitEdit();
    }
    if (!m_edit) {
        Edit edit;
        edit.document = document.GetId();
        edit.name = name;
        for (const Entity* entity : entities) {
            const Pose pose = Pose::Of(entity->GetTransform());
            edit.transforms.push_back({entity->GetId(), pose, pose});
        }
        m_edit = std::move(edit);
    }
    m_edit->widget = ImGui::GetActiveID();
}

void EntityInspector::BeginComponentEdit(SceneDocument& document, const Card& card, std::vector<std::string> before) {
    const bool same = m_edit && m_edit->document == document.GetId() && m_edit->components == card.components;
    if (m_edit && !same) {
        CommitEdit();
    }
    if (!m_edit) {
        Edit edit;
        edit.document = document.GetId();
        edit.name = "Edit " + MakeDisplayName(card.typeName);
        edit.components = card.components;
        edit.before = std::move(before);
        m_edit = std::move(edit);
    }
    m_edit->widget = ImGui::GetActiveID();
}

void EntityInspector::CommitEdit() {
    Edit edit = std::move(*m_edit);
    m_edit.reset();
    SceneWorkspace* workspace = m_context.GetWorkspace();
    SceneDocument* document = workspace ? workspace->FindDocument(edit.document) : nullptr;
    if (!document) {
        return;
    }
    Scene& scene = document->GetScene();
    std::vector<std::unique_ptr<SceneCommand>> commands;
    for (std::size_t i = 0; i < edit.components.size(); ++i) {
        const auto [id, index] = edit.components[i];
        const Entity* entity = scene.GetEntity(id);
        const std::vector<Component*> components =
            entity ? entity->GetComponents<Component>() : std::vector<Component*>();
        if (index >= components.size()) {
            continue;
        }
        std::string after = SaveComponentToString(*components[index]);
        if (after != edit.before[i]) {
            commands.push_back(
                std::make_unique<EditComponentCommand>(*document, id, index, edit.before[i], std::move(after)));
        }
    }
    std::vector<SetTransformsCommand::Change> changes;
    for (SetTransformsCommand::Change change : edit.transforms) {
        if (const Entity* entity = scene.GetEntity(change.entity)) {
            change.after = Pose::Of(entity->GetTransform());
            changes.push_back(change);
        }
    }
    if (!changes.empty()) {
        commands.push_back(std::make_unique<SetTransformsCommand>(*document, edit.name, std::move(changes)));
    }
    Execute(*document, edit.name, std::move(commands));
}

void EntityInspector::Execute(SceneDocument& document, std::string name,
                              std::vector<std::unique_ptr<SceneCommand>> commands) {
    if (commands.empty()) {
        return;
    }
    if (commands.size() == 1 && commands.front()->GetName() == name) {
        document.Execute(std::move(commands.front()));
        return;
    }
    auto group = std::make_unique<SceneCommandGroup>(document, std::move(name));
    for (auto& command : commands) {
        group->Add(std::move(command));
    }
    document.Execute(std::move(group));
}

} // namespace Spot::Editor
