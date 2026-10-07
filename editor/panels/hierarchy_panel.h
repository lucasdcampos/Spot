#pragma once

#include "app/editor_context.h"
#include "panels/panel.h"

#include <spot.h>

#include <imgui.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Spot::Editor {

// The entities of the scene being edited, as a tree: selecting, renaming, reordering, reparenting, creating, copying
// and deleting them.
class HierarchyPanel final : public Panel {
public:
    explicit HierarchyPanel(EditorContext& context);

    // Starts renaming the entity in place.
    void BeginRename(EntityId entity);
    // Where the entity's row was last drawn, in screen coordinates, as {min, max}; nothing when it was not shown.
    std::optional<std::pair<ImVec2, ImVec2>> GetRowRect(EntityId entity) const;

protected:
    void OnDraw() override;
    ImGuiWindowFlags GetWindowFlags() const override { return ImGuiWindowFlags_NoScrollbar; }

private:
    // Where a dragged entity would go when dropped on another.
    enum class DropPlace { Before, Inside, After };

    void DrawToolbar(SceneDocument& document);
    void DrawTree(SceneDocument& document);
    void DrawEntity(SceneDocument& document, Entity& entity, int depth);
    void DrawFiltered(SceneDocument& document);
    void DrawRow(SceneDocument& document, Entity& entity, bool hasChildren, bool& open);
    void DrawRename(SceneDocument& document, Entity& entity);
    void DrawContextMenu(SceneDocument& document, Entity* entity);
    void DrawCreateMenu(SceneDocument& document, EntityId parent);
    void HandleDrop(SceneDocument& document, Entity& target);
    void HandleClick(SceneDocument& document, Entity& entity);
    void HandleKeyboard(SceneDocument& document);
    void Move(SceneDocument& document, EntityId parent, EntityId before);

    EditorContext& m_context;
    std::string m_search;
    // The entities in the order the tree showed them last frame, for selecting ranges and moving with the arrows.
    std::vector<EntityId> m_visible;
    std::vector<EntityId> m_drawn;
    std::unordered_map<EntityId, std::pair<ImVec2, ImVec2>> m_rowRects;
    EntityId m_anchor = EntityId::Invalid;
    EntityId m_renaming = EntityId::Invalid;
    std::string m_renameText;
    bool m_renameFocus = false;
    // Parents to open so the selection shows, after it changed elsewhere.
    std::uint64_t m_selectionVersion = 0;
    std::unordered_set<EntityId> m_toOpen;
    EntityId m_scrollTo = EntityId::Invalid;
    // Tree nodes get ids from their entities' alone, so they stay open when moved around.
    ImGuiID m_treeSeed = 0;
};

} // namespace Spot::Editor
