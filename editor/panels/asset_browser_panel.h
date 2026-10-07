#pragma once

#include "app/editor_context.h"
#include "assets/asset_browser_model.h"
#include "panels/panel.h"

#include <spot.h>

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Spot::Editor {

// The project's assets, by folder: browsing and searching them, creating, importing, moving, renaming, duplicating
// and deleting them, and dragging them to scenes and fields.
class AssetBrowserPanel final : public Panel {
public:
    explicit AssetBrowserPanel(EditorContext& context);
    ~AssetBrowserPanel() override;

    // Copies a file dropped from the OS into the assets on the next frame: into the folder whose tile is under the
    // position, in screen coordinates, or else into the folder shown.
    void DropFile(std::filesystem::path file, ImVec2 position);

    // The folder shown, as a path relative to the assets directory; "" for the assets directory itself.
    const std::string& GetFolder() const { return m_folder; }
    void SetFolder(std::string folder);
    // The selected items, by key (see BrowserItem::GetKey()).
    const std::vector<std::string>& GetSelection() const { return m_selection; }
    // Where an item's tile was drawn last frame, in screen coordinates, as {min, max}, by key, for tests to point at.
    std::optional<std::pair<ImVec2, ImVec2>> GetTileRect(std::string_view key) const;
    // The same for other things drawn last frame: "Create", the items of its menu, as "Create/Folder", and
    // "DeletePrompt/Delete".
    std::optional<std::pair<ImVec2, ImVec2>> GetItemRect(std::string_view name) const;

protected:
    void OnDraw() override;
    ImGuiWindowFlags GetWindowFlags() const override { return ImGuiWindowFlags_NoScrollbar; }

private:
    // New assets the browser can create.
    enum class NewAsset { Folder, Scene, Material };

    struct Tile {
        std::string key;
        std::string folder;
        ImVec2 min;
        ImVec2 max;
    };

    void Rebuild(EditorProject& project);
    void DrawToolbar(EditorProject& project);
    void DrawCreateItems(EditorProject& project);
    void DrawBreadcrumbs(EditorProject& project);
    void DrawFolderTree(EditorProject& project);
    void DrawFolderNode(EditorProject& project, const std::string& folder);
    void DrawGrid(EditorProject& project);
    void DrawTile(EditorProject& project, const BrowserItem& item, float size);
    void DrawItemMenu(EditorProject& project);
    void DrawDeletePrompt(EditorProject& project);
    void HandleKeyboard(EditorProject& project);
    void HandleDrops(EditorProject& project);
    // Takes drags of assets and folders onto a folder, which moves them there.
    void AcceptMoves(EditorProject& project, const std::string& folder);

    void Select(const BrowserItem& item);
    void Open(EditorProject& project, const BrowserItem& item);
    void BeginRename(const BrowserItem& item);
    void CommitRename(EditorProject& project);
    void Create(EditorProject& project, NewAsset kind);
    void DuplicateSelection(EditorProject& project);
    void DeleteConfirmed(EditorProject& project);
    void Move(EditorProject& project, const std::vector<std::string>& paths, const std::string& folder);
    void Import(EditorProject& project, std::span<const std::filesystem::path> files, const std::string& folder);
    void BrowseForImport();
    // Imports the changes and selects what is at the paths.
    void AfterChange(EditorProject& project, const std::vector<std::string>& select);

    const BrowserItem* FindItem(std::string_view key) const;
    std::vector<const BrowserItem*> GetSelectedItems() const;
    // The source paths of the selected files and folders, without sub-assets.
    std::vector<std::string> GetSelectedPaths() const;

    EditorContext& m_context;
    std::string m_folder;
    std::string m_search;
    float m_tileSize;
    // What the grid shows, rebuilt when the assets, the folder, the search or the expanded files change.
    std::vector<std::string> m_folders;
    std::vector<BrowserItem> m_items;
    std::uint64_t m_builtVersion = 0;
    bool m_dirty = true;
    std::unordered_set<AssetId> m_expanded;
    bool m_revealFolder = true;

    std::vector<std::string> m_selection;
    std::string m_anchor;
    std::string m_scrollTo;
    std::string m_renaming;
    std::string m_renameText;
    std::optional<std::string> m_renameError;
    bool m_renameFocus = false;
    std::vector<std::string> m_toDelete;
    bool m_openDeletePrompt = false;
    bool m_openItemMenu = false;
    std::vector<std::pair<std::filesystem::path, ImVec2>> m_drops;
    std::vector<Tile> m_tiles;
    std::unordered_map<std::string, std::pair<ImVec2, ImVec2>> m_rects;
    int m_columns = 1;
    // OS dialogs answer later; they only reach the panel while this is alive.
    std::shared_ptr<AssetBrowserPanel*> m_self;
};

} // namespace Spot::Editor
