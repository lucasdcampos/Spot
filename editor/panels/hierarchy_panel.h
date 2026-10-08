#pragma once

#include "app/editor_context.h"
#include "panels/panel.h"

#include <spot.h>

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <span>
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

    // An entity's row in the tree as it shows: those of closed nodes' descendants are left out.
    struct Row {
        Entity* entity = nullptr;
        int depth = 0;
        bool hasChildren = false;
        // Whether it is the last of its siblings, where the line to it ends.
        bool last = false;
        // The tree's vertical lines that go on past the row, one bit per depth from the root's children's.
        std::uint64_t lines = 0;
    };

    void DrawToolbar(SceneDocument& document);
    // The rows of the open part of the tree, opening the parents of what should show.
    void CollectTree(SceneDocument& document);
    void CollectFiltered(SceneDocument& document);
    // Draws the rows that are in sight; the others only take their room.
    void DrawRows(SceneDocument& document);
    void DrawRow(SceneDocument& document, const Row& row);
    void DrawTreeLines(const Row& row, ImVec2 rowMin, ImVec2 rowMax, float nodeX) const;
    ImGuiID GetNodeId(EntityId entity) const;
    void DrawRename(SceneDocument& document, Entity& entity);
    void DrawContextMenu(SceneDocument& document, Entity* entity);
    void DrawCreateMenu(SceneDocument& document, EntityId parent);
    void HandleDrop(SceneDocument& document, Entity& target);
    // Assets dropped on an entity, or below the entities when it is null: materials go to its meshes, meshes and
    // models become entities, and scenes open.
    void DropAssets(SceneDocument& document, std::span<const AssetId> assets, Entity* target);
    void HandleClick(SceneDocument& document, Entity& entity);
    void HandleKeyboard(SceneDocument& document);
    void Move(SceneDocument& document, EntityId parent, EntityId before);

    EditorContext& m_context;
    std::string m_search;
    // The entities in the order the tree showed them last frame, for selecting ranges and moving with the arrows.
    std::vector<EntityId> m_visible;
    std::vector<EntityId> m_drawn;
    std::vector<Row> m_rows;
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
