#include "panels/hierarchy_panel.h"

#include "core/text.h"
#include "imgui/drag_payloads.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "scene/entity_actions.h"
#include "scene/scene_commands.h"
#include "scene/scene_workspace.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <format>
#include <memory>
#include <span>
#include <unordered_set>

namespace Spot::Editor {

namespace {

void* ToImGuiId(EntityId id) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(id));
}

// The entities a drag carries: the selection when the dragged entity is part of it, or just that entity.
std::vector<EntityId> GetDragged(SceneDocument& document, Entity& entity) {
    if (!document.GetSelection().Contains(entity.GetId())) {
        return {entity.GetId()};
    }
    std::vector<EntityId> ids;
    for (const Entity* selected : GetSelectedEntities(document)) {
        ids.push_back(selected->GetId());
    }
    return ids;
}

// The sibling that comes after the entity, or Invalid when it is the last one.
EntityId GetNextSibling(const Entity& entity) {
    const std::vector<Entity*> siblings =
        entity.GetParent() ? entity.GetParent()->GetChildren() : entity.GetScene().GetRootEntities();
    const auto found = std::ranges::find(siblings, &entity);
    return found != siblings.end() && found + 1 != siblings.end() ? (*(found + 1))->GetId() : EntityId::Invalid;
}

} // namespace

HierarchyPanel::HierarchyPanel(EditorContext& context)
    : Panel("Hierarchy", ICON_FA_LIST "  Hierarchy"), m_context(context) {
}

void HierarchyPanel::BeginRename(EntityId entity) {
    SceneDocument* document = m_context.GetActiveDocument();
    const Entity* found = document ? document->GetScene().GetEntity(entity) : nullptr;
    if (!found) {
        return;
    }
    m_renaming = entity;
    m_renameText = found->GetName();
    m_renameFocus = true;
}

std::optional<std::pair<ImVec2, ImVec2>> HierarchyPanel::GetRowRect(EntityId entity) const {
    const auto found = m_rowRects.find(entity);
    return found != m_rowRects.end() ? std::optional(found->second) : std::nullopt;
}

void HierarchyPanel::OnDraw() {
    SceneDocument* document = m_context.GetActiveDocument();
    if (!document) {
        DrawEmptyState(ICON_FA_LIST, "Open a scene to see its entities.");
        return;
    }
    // A renamed entity may have been deleted or undone away meanwhile.
    if (m_renaming != EntityId::Invalid && !document->GetScene().GetEntity(m_renaming)) {
        m_renaming = EntityId::Invalid;
    }

    DrawToolbar(*document);
    ImGui::BeginChild("##Tree", {0.0f, 0.0f}, ImGuiChildFlags_None);
    m_drawn.clear();
    m_rowRects.clear();
    m_treeSeed = ImGui::GetID("##Entities");
    if (m_search.empty()) {
        DrawTree(*document);
    } else {
        DrawFiltered(*document);
    }

    // The space below the entities: clicking it clears the selection, and dropping on it moves to the root's end.
    const ImVec2 rest{ImGui::GetContentRegionAvail().x,
                      std::max(ImGui::GetContentRegionAvail().y, ImGui::GetFrameHeight())};
    ImGui::InvisibleButton("##Below", rest);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        document->GetSelection().Clear();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EntitiesPayload)) {
            const std::span<const EntityId> ids = GetPayloadEntities(*payload);
            document->Execute(std::make_unique<ReparentEntitiesCommand>(
                *document, std::vector<EntityId>(ids.begin(), ids.end()), EntityId::Invalid));
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("##BelowMenu")) {
        DrawContextMenu(*document, nullptr);
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    // Outside the child: clicking in it focuses the panel's window, whose shortcuts these are.
    HandleKeyboard(*document);
    m_visible = m_drawn;
    m_selectionVersion = document->GetSelection().GetVersion();
}

void HierarchyPanel::DrawToolbar(SceneDocument& document) {
    if (ImGui::Button(ICON_FA_PLUS)) {
        ImGui::OpenPopup("##CreateMenu");
    }
    ImGui::SetItemTooltip("Create an entity");
    if (ImGui::BeginPopup("##CreateMenu")) {
        DrawCreateMenu(document, EntityId::Invalid);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS " Search", &m_search);
}

void HierarchyPanel::DrawTree(SceneDocument& document) {
    // When the selection changed elsewhere, such as in a view, its entities' parents open and it scrolls into sight.
    m_toOpen.clear();
    if (document.GetSelection().GetVersion() != m_selectionVersion) {
        for (const EntityId id : document.GetSelection().GetEntities()) {
            const Entity* entity = document.GetScene().GetEntity(id);
            for (const Entity* parent = entity ? entity->GetParent() : nullptr; parent; parent = parent->GetParent()) {
                m_toOpen.insert(parent->GetId());
            }
        }
        m_scrollTo = document.GetSelection().GetPrimary();
    }
    for (Entity* root : document.GetScene().GetRootEntities()) {
        DrawEntity(document, *root, 0);
    }
}

void HierarchyPanel::DrawEntity(SceneDocument& document, Entity& entity, int depth) {
    const std::vector<Entity*> children = entity.GetChildren();
    if (m_toOpen.contains(entity.GetId())) {
        ImGui::SetNextItemOpen(true);
    }
    bool open = false;
    DrawRow(document, entity, !children.empty(), open);
    if (open) {
        for (Entity* child : children) {
            DrawEntity(document, *child, depth + 1);
        }
        ImGui::TreePop();
    }
}

void HierarchyPanel::DrawFiltered(SceneDocument& document) {
    std::vector<Entity*> matches;
    document.GetScene().ForEachEntity([&](Entity& entity) {
        if (ContainsIgnoringCase(entity.GetName(), m_search)) {
            matches.push_back(&entity);
        }
    });
    if (matches.empty()) {
        ImGui::TextColored(EditorColors::TextMuted, "No entity is named like that.");
        return;
    }
    for (Entity* entity : matches) {
        bool open = false;
        DrawRow(document, *entity, false, open);
        if (open) {
            ImGui::TreePop();
        }
    }
}

void HierarchyPanel::DrawRow(SceneDocument& document, Entity& entity, bool hasChildren, bool& open) {
    const EntityId id = entity.GetId();
    const bool selected = document.GetSelection().Contains(id);
    const bool renaming = m_renaming == id;
    m_drawn.push_back(id);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth |
                               ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_DrawLinesToNodes;
    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const ImVec4 accent = EditorColors::Accent;
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(accent.x, accent.y, accent.z, 0.32f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, selected ? ImVec4(accent.x, accent.y, accent.z, 0.42f)
                                                           : ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    ImGui::PushStyleColor(ImGuiCol_Text, entity.IsActiveInHierarchy() ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                                                      : EditorColors::TextMuted);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        {ImGui::GetStyle().FramePadding.x, 2.0f * ImGui::GetStyle().FontScaleDpi});
    ImGui::SetNextItemAllowOverlap();
    const std::string label =
        renaming ? std::string(GetEntityIcon(entity)) : std::format("{}  {}", GetEntityIcon(entity), entity.GetName());
    // The node's id comes from the entity's alone, so its open state survives renames and moves.
    open = ImGui::TreeNodeBehavior(ImHashData(&id, sizeof(id), m_treeSeed), flags, label.c_str());
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    // After the node, which pushes its own id when open, so the two are popped in order.
    ImGui::PushID(ToImGuiId(id));

    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const ImVec2 rowMax = ImGui::GetItemRectMax();
    m_rowRects[id] = {rowMin, rowMax};
    if (m_scrollTo == id) {
        ImGui::SetScrollHereY(0.5f);
        m_scrollTo = EntityId::Invalid;
    }

    if (!renaming) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
            HandleClick(document, entity);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsItemToggledOpen()) {
            if (SceneView* view = m_context.GetWorkspace() ? m_context.GetWorkspace()->GetActiveView() : nullptr) {
                document.GetSelection().Select(id);
                view->FrameSelection();
            }
        }
        if (ImGui::BeginDragDropSource()) {
            const std::vector<EntityId> dragged = GetDragged(document, entity);
            ImGui::SetDragDropPayload(EntitiesPayload, dragged.data(), dragged.size() * sizeof(EntityId));
            if (dragged.size() == 1) {
                ImGui::Text("%s  %s", GetEntityIcon(entity), entity.GetName().c_str());
            } else {
                ImGui::Text("%s  %zu entities", ICON_FA_LAYER_GROUP, dragged.size());
            }
            ImGui::EndDragDropSource();
        }
        if (m_search.empty()) {
            HandleDrop(document, entity);
        }
        if (ImGui::BeginPopupContextItem("##EntityMenu")) {
            if (!selected) {
                document.GetSelection().Select(id);
            }
            DrawContextMenu(document, &entity);
            ImGui::EndPopup();
        }
    } else {
        DrawRename(document, entity);
    }

    // The eye at the row's end shows and hides the entity; it stays visible while the entity is hidden.
    const bool activeSelf = entity.IsActiveSelf();
    if (ImGui::IsMouseHoveringRect(rowMin, rowMax) || !activeSelf) {
        const char* eye = activeSelf ? ICON_FA_EYE : ICON_FA_EYE_SLASH;
        const float width = ImGui::CalcTextSize(ICON_FA_EYE_SLASH).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorScreenPos({rowMax.x - width, rowMin.y});
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              activeSelf ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : EditorColors::TextMuted);
        if (ImGui::Button(eye, {width, rowMax.y - rowMin.y})) {
            const std::vector<EntityId> targets = selected ? GetDragged(document, entity) : std::vector<EntityId>{id};
            document.Execute(std::make_unique<SetEntitiesActiveCommand>(document, targets, !activeSelf));
        }
        ImGui::PopStyleColor(2);
        ImGui::SetItemTooltip(activeSelf ? "Deactivate" : "Activate");
    }
    ImGui::PopID();
}

void HierarchyPanel::DrawRename(SceneDocument& document, Entity& entity) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (m_renameFocus) {
        ImGui::SetKeyboardFocusHere();
        m_renameFocus = false;
    }
    const bool committed = ImGui::InputText("##Rename", &m_renameText,
                                            ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
    const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (committed || (ImGui::IsItemDeactivated() && !cancelled)) {
        if (!m_renameText.empty()) {
            document.Execute(std::make_unique<RenameEntityCommand>(document, entity.GetId(), m_renameText));
        }
        m_renaming = EntityId::Invalid;
    } else if (cancelled || ImGui::IsItemDeactivated()) {
        m_renaming = EntityId::Invalid;
    }
}

void HierarchyPanel::HandleClick(SceneDocument& document, Entity& entity) {
    Selection& selection = document.GetSelection();
    const ImGuiIO& io = ImGui::GetIO();
    const EntityId id = entity.GetId();
    if (io.KeyShift && m_anchor != EntityId::Invalid) {
        const auto from = std::ranges::find(m_visible, m_anchor);
        const auto to = std::ranges::find(m_visible, id);
        if (from != m_visible.end() && to != m_visible.end()) {
            std::vector<EntityId> range(std::min(from, to), std::max(from, to) + 1);
            if (io.KeyCtrl) {
                for (const EntityId added : range) {
                    selection.Add(added);
                }
            } else {
                selection.Set(range);
            }
            selection.Add(id);
            return;
        }
    }
    if (io.KeyCtrl) {
        selection.Toggle(id);
    } else {
        selection.Select(id);
    }
    m_anchor = id;
}

void HierarchyPanel::HandleDrop(SceneDocument& document, Entity& target) {
    if (!ImGui::BeginDragDropTarget()) {
        return;
    }
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float height = max.y - min.y;
    const float y = ImGui::GetMousePos().y;
    const DropPlace place = y < min.y + height * 0.25f   ? DropPlace::Before
                            : y > max.y - height * 0.25f ? DropPlace::After
                                                         : DropPlace::Inside;
    constexpr ImGuiDragDropFlags flags =
        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EntitiesPayload, flags)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 color = ImGui::GetColorU32(EditorColors::Accent);
        const float thickness = 2.0f * ImGui::GetStyle().FontScaleDpi;
        if (place == DropPlace::Inside) {
            drawList->AddRect(min, max, color, ImGui::GetStyle().FrameRounding, thickness);
        } else {
            const float lineY = place == DropPlace::Before ? min.y : max.y;
            drawList->AddLine({min.x, lineY}, {max.x, lineY}, color, thickness);
        }
        if (payload->IsDelivery()) {
            const std::span<const EntityId> ids = GetPayloadEntities(*payload);
            std::vector<EntityId> moved(ids.begin(), ids.end());
            const EntityId parent = target.GetParent() ? target.GetParent()->GetId() : EntityId::Invalid;
            std::unique_ptr<ReparentEntitiesCommand> command;
            switch (place) {
            case DropPlace::Before:
                command = std::make_unique<ReparentEntitiesCommand>(document, moved, parent, target.GetId());
                break;
            case DropPlace::After:
                command = std::make_unique<ReparentEntitiesCommand>(document, moved, parent, GetNextSibling(target));
                break;
            case DropPlace::Inside:
                command = std::make_unique<ReparentEntitiesCommand>(document, moved, target.GetId());
                break;
            }
            document.Execute(std::move(command));
        }
    }
    ImGui::EndDragDropTarget();
}

void HierarchyPanel::HandleKeyboard(SceneDocument& document) {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) {
        return;
    }
    HandleEntityShortcuts(document);
    if (ImGui::Shortcut(ImGuiKey_F2)) {
        BeginRename(document.GetSelection().GetPrimary());
    }
    // The arrows move the selection along the tree as it is shown.
    const int step = ImGui::Shortcut(ImGuiKey_DownArrow, ImGuiInputFlags_Repeat) ? 1
                     : ImGui::Shortcut(ImGuiKey_UpArrow, ImGuiInputFlags_Repeat) ? -1
                                                                                 : 0;
    if (step != 0 && !m_visible.empty()) {
        const auto current = std::ranges::find(m_visible, document.GetSelection().GetPrimary());
        std::ptrdiff_t index = current == m_visible.end() ? 0 : (current - m_visible.begin()) + step;
        index = std::clamp<std::ptrdiff_t>(index, 0, static_cast<std::ptrdiff_t>(m_visible.size()) - 1);
        const EntityId next = m_visible[static_cast<std::size_t>(index)];
        document.GetSelection().Select(next);
        m_anchor = next;
        m_scrollTo = next;
    }
}

void HierarchyPanel::DrawCreateMenu(SceneDocument& document, EntityId parent) {
    // New roots appear where the view looks; new children at their parent.
    Vector3 position;
    if (parent == EntityId::Invalid && m_context.GetWorkspace() && m_context.GetWorkspace()->GetActiveView()) {
        position = m_context.GetWorkspace()->GetActiveView()->GetCamera().GetPivot();
    }
    const auto item = [&](const EntityTemplateInfo& info) {
        if (ImGui::MenuItem(std::format("{}  {}", info.icon, info.name).c_str())) {
            CreateEntity(document, info.kind, parent, position);
        }
    };
    for (const EntityTemplateInfo& info : GetEntityTemplates()) {
        if (!info.category) {
            item(info);
        }
    }
    for (const char* category : {"3D Object", "Light"}) {
        if (ImGui::BeginMenu(category)) {
            for (const EntityTemplateInfo& info : GetEntityTemplates()) {
                if (info.category && std::string_view(info.category) == category) {
                    item(info);
                }
            }
            ImGui::EndMenu();
        }
    }
}

void HierarchyPanel::DrawContextMenu(SceneDocument& document, Entity* entity) {
    const auto shortcut = [](ImGuiKeyChord chord) { return ImGui::GetKeyChordName(chord); };
    if (ImGui::BeginMenu(entity ? ICON_FA_PLUS "  Create Child" : ICON_FA_PLUS "  Create")) {
        DrawCreateMenu(document, entity ? entity->GetId() : EntityId::Invalid);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    const bool hasSelection = !document.GetSelection().IsEmpty();
    if (entity && ImGui::MenuItem(ICON_FA_PEN "  Rename", shortcut(ImGuiKey_F2))) {
        BeginRename(entity->GetId());
    }
    if (ImGui::MenuItem(ICON_FA_CLONE "  Duplicate", shortcut(ImGuiMod_Ctrl | ImGuiKey_D), false, hasSelection)) {
        DuplicateSelection(document);
    }
    if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Delete", shortcut(ImGuiKey_Delete), false, hasSelection)) {
        DeleteSelection(document);
    }
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_SCISSORS "  Cut", shortcut(ImGuiMod_Ctrl | ImGuiKey_X), false, hasSelection)) {
        CutSelection(document);
    }
    if (ImGui::MenuItem(ICON_FA_COPY "  Copy", shortcut(ImGuiMod_Ctrl | ImGuiKey_C), false, hasSelection)) {
        CopySelection(document);
    }
    const bool canPaste = Clipboard::HasText();
    if (ImGui::MenuItem(ICON_FA_PASTE "  Paste", shortcut(ImGuiMod_Ctrl | ImGuiKey_V), false, canPaste)) {
        PasteClipboard(document);
    }
    if (entity && ImGui::MenuItem("Paste as Child", nullptr, false, canPaste)) {
        PasteClipboard(document, true);
    }
    if (entity) {
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_EXPAND "  Frame", "F")) {
            if (SceneView* view = m_context.GetWorkspace() ? m_context.GetWorkspace()->GetActiveView() : nullptr) {
                view->FrameSelection();
            }
        }
    }
}

} // namespace Spot::Editor
