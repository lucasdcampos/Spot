#include "panels/asset_browser_panel.h"

#include "assets/asset_list.h"
#include "assets/plain_files.h"
#include "assets/thumbnail_cache.h"
#include "core/editor_log.h"
#include "imgui/asset_icons.h"
#include "imgui/drag_payloads.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/imgui_renderer.h"
#include "imgui/widgets.h"
#include "project/project_templates.h"
#include "scene/scene_workspace.h"

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <system_error>

namespace Spot::Editor {

namespace {

constexpr float MinTileSize = 56.0f;
constexpr float MaxTileSize = 160.0f;
// The front of the painted folders.
constexpr ImVec4 FolderColor{0.839f, 0.706f, 0.478f, 1.0f};
// The caption under a tile's name, which says what the asset is, relative to the font size.
constexpr float CaptionScale = 0.85f;
// How often the code is listed again while shown, in seconds, since it changes outside the editor without telling.
constexpr double CodeListInterval = 1.0;

const std::array ImportFilters{
    FileFilter{"Assets", "png;jpg;jpeg;bmp;tga;gltf;glb;bin;wav;ogg;ttf;otf;material;scene"},
    FileFilter{"All files", "*"},
};

const char* GetItemIcon(const BrowserItem& item) {
    if (item.folder) {
        return ICON_FA_FOLDER;
    }
    return item.root == BrowserRoot::Code ? ICON_FA_FILE_CODE : GetAssetTypeIcon(item.type);
}

std::filesystem::path GetRootDirectory(const EditorProject& project, BrowserRoot root) {
    return root == BrowserRoot::Code ? project.GetProject().GetCodeDirectory()
                                     : project.GetProject().GetAssetsDirectory();
}

bool HasCodeDirectory(const EditorProject& project) {
    std::error_code error;
    return std::filesystem::is_directory(project.GetProject().GetCodeDirectory(), error);
}

// Runs the operation on the files of the root: the asset database, which keeps meta files with their files, or the
// plain files of the code.
template <typename Operation>
auto WithFiles(EditorProject& project, BrowserRoot root, Operation&& operation) {
    if (root == BrowserRoot::Code) {
        PlainFiles files(project.GetProject().GetCodeDirectory());
        return operation(files);
    }
    return operation(project.GetAssetDatabase());
}

// The root's name as on disk, such as "assets", after its icon.
std::string GetRootLabel(const EditorProject& project, BrowserRoot root) {
    const std::string name = PathToUtf8(GetRootDirectory(project, root).filename().u8string());
    return (root == BrowserRoot::Code ? ICON_FA_CODE "  " : ICON_FA_FOLDER_OPEN "  ") + name;
}

// The path as people know it: code paths start with the code folder.
std::string GetShownPath(BrowserRoot root, const std::string& path) {
    return root == BrowserRoot::Code ? JoinPath("src", path) : path;
}

// The text cut to fit the width, ending with "..." when it does not.
std::string EllipsizeEnd(const std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) {
        return text;
    }
    const float available = width - ImGui::CalcTextSize("...").x;
    std::size_t end = text.size();
    while (end > 0 && ImGui::CalcTextSize(text.data(), text.data() + end).x > available) {
        --end;
        // UTF-8 continuation bytes are 10xxxxxx; a character is cut whole.
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
            --end;
        }
    }
    return text.substr(0, end) + "...";
}

// The paths a drag of paths carries, one per line.
std::vector<std::string> GetPayloadPaths(const ImGuiPayload& payload) {
    std::vector<std::string> paths;
    const std::string_view text(static_cast<const char*>(payload.Data), static_cast<std::size_t>(payload.DataSize));
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        if (end > start) {
            paths.emplace_back(text.substr(start, end - start));
        }
        start = end + 1;
    }
    return paths;
}

std::string GetExtension(const std::string& name) {
    const std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? std::string() : name.substr(dot);
}

} // namespace

AssetBrowserPanel::AssetBrowserPanel(EditorContext& context)
    : Panel("Assets", ICON_FA_FOLDER_OPEN "  Assets"), m_context(context), m_tileSize(88.0f),
      m_self(std::make_shared<AssetBrowserPanel*>(this)) {
}

AssetBrowserPanel::~AssetBrowserPanel() {
    *m_self = nullptr;
}

void AssetBrowserPanel::DropFile(std::filesystem::path file, ImVec2 position) {
    m_drops.emplace_back(std::move(file), position);
}

void AssetBrowserPanel::SetFolder(BrowserRoot root, std::string folder) {
    if (root == m_root && folder == m_folder && m_search.empty()) {
        return;
    }
    m_root = root;
    m_folder = std::move(folder);
    m_search.clear();
    m_selection.clear();
    m_anchor.clear();
    m_renaming.clear();
    m_revealFolder = true;
    m_dirty = true;
}

std::optional<std::pair<ImVec2, ImVec2>> AssetBrowserPanel::GetTileRect(std::string_view key) const {
    const auto found = std::ranges::find(m_tiles, key, &Tile::key);
    return found != m_tiles.end() ? std::optional(std::pair(found->min, found->max)) : std::nullopt;
}

std::optional<std::pair<ImVec2, ImVec2>> AssetBrowserPanel::GetItemRect(std::string_view name) const {
    const auto found = m_rects.find(std::string(name));
    return found != m_rects.end() ? std::optional(found->second) : std::nullopt;
}

const BrowserItem* AssetBrowserPanel::FindItem(std::string_view key) const {
    const auto found = std::ranges::find_if(m_items, [&](const BrowserItem& item) { return item.GetKey() == key; });
    return found != m_items.end() ? &*found : nullptr;
}

std::vector<const BrowserItem*> AssetBrowserPanel::GetSelectedItems() const {
    std::vector<const BrowserItem*> items;
    for (const BrowserItem& item : m_items) {
        if (std::ranges::find(m_selection, item.GetKey()) != m_selection.end()) {
            items.push_back(&item);
        }
    }
    return items;
}

std::vector<AssetBrowserPanel::Location> AssetBrowserPanel::GetSelectedPaths() const {
    std::vector<Location> paths;
    for (const BrowserItem* item : GetSelectedItems()) {
        if (!item->subAsset) {
            paths.push_back({item->root, item->path});
        }
    }
    return paths;
}

const std::vector<std::string>& AssetBrowserPanel::GetFolders(BrowserRoot root) const {
    return root == BrowserRoot::Code ? m_codeFolders : m_folders;
}

void AssetBrowserPanel::Rebuild(EditorProject& project) {
    m_builtVersion = project.GetVersion();
    m_dirty = false;
    m_codeListed = ImGui::GetTime();
    const AssetDatabase& database = project.GetAssetDatabase();
    const std::filesystem::path code = project.GetProject().GetCodeDirectory();
    m_folders = ListFolders(project.GetProject().GetAssetsDirectory());
    m_codeFolders = ListFolders(code);
    if (m_root == BrowserRoot::Code && !HasCodeDirectory(project)) {
        m_root = BrowserRoot::Assets;
        m_folder.clear();
    }
    // A folder that went away, such as one deleted outside the editor, leaves its nearest parent shown.
    const std::vector<std::string>& folders = GetFolders(m_root);
    while (!m_folder.empty() && std::ranges::find(folders, m_folder) == folders.end()) {
        m_folder = GetParentFolder(m_folder);
    }

    if (!m_search.empty()) {
        m_items = SearchAssets(database, m_folders, m_search);
        for (BrowserItem& item : SearchCode(code, m_codeFolders, m_search)) {
            m_items.push_back(std::move(item));
        }
    } else if (m_root == BrowserRoot::Code) {
        m_items = ListCodeFolder(code, m_codeFolders, m_folder);
    } else {
        m_items.clear();
        for (BrowserItem& item : ListFolder(database, m_folders, m_folder)) {
            const bool expanded = item.hasSubAssets && m_expanded.contains(item.id);
            const AssetId id = item.id;
            m_items.push_back(std::move(item));
            if (expanded) {
                for (BrowserItem& part : ListSubAssets(database, id)) {
                    m_items.push_back(std::move(part));
                }
            }
        }
    }
    // Selected items that are gone stay unselected.
    std::erase_if(m_selection, [&](const std::string& key) { return !FindItem(key); });
}

void AssetBrowserPanel::OnDraw() {
    EditorProject* project = m_context.GetProject();
    if (!project) {
        DrawEmptyState(ICON_FA_FOLDER_OPEN, "Open a project to browse its assets.");
        return;
    }
    HandleDrops(*project);
    m_rects.clear();
    const bool codeShown = m_root == BrowserRoot::Code || !m_search.empty();
    if (m_dirty || m_builtVersion != project->GetVersion() ||
        (codeShown && ImGui::GetTime() - m_codeListed >= CodeListInterval)) {
        Rebuild(*project);
    }

    DrawToolbar(*project);
    ImGui::Separator();

    const float height = ImGui::GetContentRegionAvail().y;
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("##Browser", 2, flags)) {
        ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 13.0f);
        ImGui::TableSetupColumn("Items", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::BeginChild("##Folders", {0.0f, height})) {
            DrawFolderTree(*project);
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        const ImGuiWindowFlags gridFlags = ImGui::GetIO().KeyCtrl ? ImGuiWindowFlags_NoScrollWithMouse : 0;
        if (ImGui::BeginChild("##Grid", {0.0f, height}, ImGuiChildFlags_None, gridFlags)) {
            DrawGrid(*project);
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }

    HandleKeyboard(*project);
    DrawDeletePrompt(*project);
    DrawComponentDialog(*project);
    if (auto move = std::exchange(m_pendingMove, std::nullopt)) {
        Move(*project, move->root, move->paths, move->folder);
    }
}

void AssetBrowserPanel::DrawToolbar(EditorProject& project) {
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    if (ImGui::Button(ICON_FA_PLUS "  Create")) {
        ImGui::OpenPopup("##Create");
    }
    m_rects["Create"] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    if (ImGui::BeginPopup("##Create")) {
        DrawCreateItems(project);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FILE_IMPORT "  Import")) {
        BrowseForImport();
    }
    ImGui::SetItemTooltip("Copy files into this folder; dropping them from the OS does it too");
    ImGui::SameLine();
    DrawBreadcrumbs(project);

    // The search and the tile size, on the right.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float searchWidth = ImGui::GetFontSize() * 12.0f;
    const float sizeWidth = ImGui::GetFontSize() * 5.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - searchWidth - sizeWidth - style.ItemSpacing.x));
    ImGui::SetNextItemWidth(searchWidth);
    if (ImGui::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS "  Search assets and code", &m_search)) {
        m_selection.clear();
        m_dirty = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(sizeWidth);
    ImGui::SliderFloat("##TileSize", &m_tileSize, MinTileSize, MaxTileSize, "");
    ImGui::SetItemTooltip("Tile size; Ctrl and the mouse wheel change it too");
}

void AssetBrowserPanel::DrawCreateItems(EditorProject& project) {
    const auto item = [&](const char* icon, const char* label, NewAsset kind) {
        if (ImGui::MenuItem((std::string(icon) + "  " + label).c_str())) {
            Create(project, kind);
        }
        m_rects[std::string("Create/") + label] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    };
    item(ICON_FA_FOLDER, "Folder", NewAsset::Folder);
    // The code has no assets.
    if (m_root == BrowserRoot::Assets) {
        ImGui::Separator();
        item(ICON_FA_MOUNTAIN_SUN, "Scene", NewAsset::Scene);
        item(ICON_FA_PALETTE, "Material", NewAsset::Material);
    }
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_CODE "  C++ Component")) {
        m_openComponentDialog = true;
    }
    ImGui::SetItemTooltip("A component class of the game's own, in the project's code");
    m_rects["Create/C++ Component"] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
}

void AssetBrowserPanel::DrawBreadcrumbs(EditorProject& project) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    std::string path;
    const auto crumb = [&](const char* label, const std::string& folder, bool last) {
        ImGui::PushID(folder.c_str());
        if (ImGui::Button(label) && !last) {
            SetFolder(m_root, folder);
        }
        AcceptMoves(project, m_root, folder);
        ImGui::PopID();
    };
    const bool searching = !m_search.empty();
    crumb(GetRootLabel(project, m_root).c_str(), "", m_folder.empty() && !searching);
    std::size_t start = 0;
    while (!m_folder.empty() && start <= m_folder.size()) {
        const std::size_t end = std::min(m_folder.find('/', start), m_folder.size());
        path = m_folder.substr(0, end);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextDisabled(ICON_FA_CHEVRON_RIGHT);
        ImGui::SameLine(0.0f, 0.0f);
        crumb(m_folder.substr(start, end - start).c_str(), path, end == m_folder.size() && !searching);
        start = end + 1;
    }
    if (searching) {
        ImGui::SameLine();
        ImGui::TextDisabled("Search results");
    }
    ImGui::PopStyleColor();
}

void AssetBrowserPanel::DrawFolderTree(EditorProject& project) {
    DrawRoot(project, BrowserRoot::Assets);
    if (HasCodeDirectory(project)) {
        DrawRoot(project, BrowserRoot::Code);
    }
    m_revealFolder = false;
}

void AssetBrowserPanel::DrawRoot(EditorProject& project, BrowserRoot root) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (m_root == root && m_folder.empty() && m_search.empty()) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    ImGui::PushID(static_cast<int>(root));
    const bool open = ImGui::TreeNodeEx("##Root", flags, "%s", GetRootLabel(project, root).c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        SetFolder(root, {});
    }
    m_rects[root == BrowserRoot::Code ? "Tree/Code" : "Tree/Assets"] = {ImGui::GetItemRectMin(),
                                                                        ImGui::GetItemRectMax()};
    ImGui::SetItemTooltip(root == BrowserRoot::Code ? "The game's C++ code" : "What the game loads");
    AcceptMoves(project, root, {});
    if (open) {
        for (const std::string& folder : GetSubfolders(GetFolders(root), {})) {
            DrawFolderNode(project, root, folder);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void AssetBrowserPanel::DrawFolderNode(EditorProject& project, BrowserRoot root, const std::string& folder) {
    const std::vector<std::string> subfolders = GetSubfolders(GetFolders(root), folder);
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (subfolders.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    const bool shown = m_root == root && m_search.empty();
    if (shown && folder == m_folder) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    // The folder shown is revealed when it changes, by opening the folders above it.
    if (m_revealFolder && m_root == root && IsInFolder(m_folder, folder) && folder != m_folder) {
        ImGui::SetNextItemOpen(true);
    }
    // The node spans the row and has no label of its own, so the icon can have the color of folders.
    const bool open = ImGui::TreeNodeEx(folder.c_str(), flags, "%s", "");
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        SetFolder(root, folder);
    }
    AcceptMoves(project, root, folder);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(FolderColor, ICON_FA_FOLDER);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(GetFileName(folder).c_str());
    if (open) {
        for (const std::string& subfolder : subfolders) {
            DrawFolderNode(project, root, subfolder);
        }
        ImGui::TreePop();
    }
}

void AssetBrowserPanel::DrawGrid(EditorProject& project) {
    const ImGuiIO& io = ImGui::GetIO();
    // Ctrl and the wheel zoom the tiles, while the grid does not scroll (see OnDraw()).
    if (ImGui::IsWindowHovered() && io.KeyCtrl && io.MouseWheel != 0.0f) {
        m_tileSize = std::clamp(m_tileSize + io.MouseWheel * 8.0f, MinTileSize, MaxTileSize);
    }
    m_tiles.clear();

    if (m_items.empty()) {
        if (!m_search.empty()) {
            DrawEmptyState(ICON_FA_MAGNIFYING_GLASS, "Nothing matches the search.");
        } else if (m_root == BrowserRoot::Code) {
            DrawEmptyState(ICON_FA_CODE, "This folder is empty. Drop files here, or right-click to create a folder.");
        } else {
            DrawEmptyState(ICON_FA_FOLDER_OPEN,
                           "This folder is empty. Drop files here, or right-click to create assets.");
        }
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float size = std::round(m_tileSize * style.FontScaleDpi);
    const float spacing = style.ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    m_columns = std::max(1, static_cast<int>((available + spacing) / (size + spacing)));
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        if (i % static_cast<std::size_t>(m_columns) != 0) {
            ImGui::SameLine(0.0f, spacing);
        }
        DrawTile(project, m_items[i], size);
    }

    // Clicks on nothing clear the selection; right-clicks there open the folder's menu.
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_selection.clear();
    }
    if (ImGui::BeginPopupContextWindow("##Background",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::BeginMenu(ICON_FA_PLUS "  Create")) {
            DrawCreateItems(project);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ICON_FA_FILE_IMPORT "  Import...")) {
            BrowseForImport();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_UP_RIGHT_FROM_SQUARE "  Show in Explorer")) {
            Shell::Open(GetRootDirectory(project, m_root) / Utf8ToPath(m_folder));
        }
        if (ImGui::MenuItem(ICON_FA_ROTATE "  Refresh")) {
            project.Refresh();
            m_dirty = true;
        }
        ImGui::EndPopup();
    }
    if (std::exchange(m_openItemMenu, false)) {
        ImGui::OpenPopup("##Item");
    }
    DrawItemMenu(project);
}

void AssetBrowserPanel::DrawTile(EditorProject& project, const BrowserItem& item, float size) {
    const std::string key = item.GetKey();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float labelHeight = ImGui::GetTextLineHeight();
    const float captionSize = std::round(ImGui::GetFontSize() * CaptionScale);
    const ImVec2 tileSize{size, size + labelHeight + captionSize + style.ItemInnerSpacing.y * 3.0f};
    const bool selected = std::ranges::find(m_selection, key) != m_selection.end();
    const bool code = item.root == BrowserRoot::Code;

    ImGui::PushID(key.c_str());
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##Tile", tileSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    m_tiles.push_back({key, item.root, item.folder ? item.path : std::string(), min, max});
    const bool hovered = ImGui::IsItemHovered();
    if (m_scrollTo == key) {
        ImGui::SetScrollHereY();
        m_scrollTo.clear();
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        Select(item);
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        Open(project, item);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (!selected) {
            m_selection = {key};
            m_anchor = key;
        }
        m_openItemMenu = true;
    }
    const std::string type = item.folder ? std::string("Folder")
                             : code      ? GetFileTypeName(item.name)
                                         : std::string(GetAssetTypeName(item.type));
    if (hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const std::string path = item.subAsset ? item.path + " #" + item.name : GetShownPath(item.root, item.path);
        ImGui::SetItemTooltip("%s\n%s", path.c_str(), type.c_str());
    }

    // Dragged assets go to fields, scenes and folders; folders only to folders, and code only to code folders.
    if (ImGui::BeginDragDropSource()) {
        std::vector<const BrowserItem*> dragged = selected ? GetSelectedItems() : std::vector{&item};
        std::erase_if(dragged, [&](const BrowserItem* each) { return each->root != item.root; });
        const bool onlyAssets = !code && std::ranges::none_of(dragged, &BrowserItem::folder);
        if (onlyAssets) {
            std::vector<AssetId> ids;
            for (const BrowserItem* each : dragged) {
                ids.push_back(each->id);
            }
            ImGui::SetDragDropPayload(AssetsPayload, ids.data(), ids.size() * sizeof(AssetId));
        } else {
            std::string paths;
            for (const BrowserItem* each : dragged) {
                if (!each->subAsset) {
                    paths += each->path + "\n";
                }
            }
            ImGui::SetDragDropPayload(code ? CodePathsPayload : AssetPathsPayload, paths.data(), paths.size());
        }
        if (dragged.size() == 1) {
            ImGui::Text("%s  %s", GetItemIcon(item), item.GetDisplayName().c_str());
        } else {
            ImGui::Text(ICON_FA_LAYER_GROUP "  %zu items", dragged.size());
        }
        ImGui::EndDragDropSource();
    }
    if (item.folder) {
        AcceptMoves(project, item.root, item.path);
    }
    // Painted icons take many vertices, so tiles scrolled out of view draw nothing; nor do they ask for thumbnails.
    if (!ImGui::IsItemVisible() && m_renaming != key) {
        ImGui::PopID();
        return;
    }

    // Background and icon. Files sit on a faint card; folders have none until hovered or selected.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec4 accent = EditorColors::Accent;
    if (selected || hovered || !item.folder) {
        const ImVec4 color = selected  ? ImVec4(accent.x, accent.y, accent.z, hovered ? 0.24f : 0.16f)
                             : hovered ? ImVec4(1.0f, 1.0f, 1.0f, 0.06f)
                                       : ImVec4(1.0f, 1.0f, 1.0f, 0.03f);
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(color), style.FrameRounding);
    }
    if (selected) {
        drawList->AddRect(min, max, ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.65f)),
                          style.FrameRounding);
    }
    if (item.subAsset) {
        // Parts of a file sit on a darker card, so they read as belonging to the file before them.
        drawList->AddRectFilled({min.x + 2.0f, min.y + 2.0f}, {max.x - 2.0f, min.y + size - 2.0f},
                                ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
    }
    ThumbnailCache* thumbnails = m_context.GetThumbnails();
    const Texture* picture =
        thumbnails && !item.folder && !code ? thumbnails->Get(project, item.id, item.type) : nullptr;
    if (picture) {
        // Fitted into the square, keeping its proportions.
        const float inset = size * (item.subAsset ? 0.16f : 0.08f);
        const float side = size - inset * 2.0f;
        const float width = static_cast<float>(picture->GetWidth());
        const float height = static_cast<float>(picture->GetHeight());
        const float scale = side / std::max(width, height);
        const ImVec2 extent{width * scale, height * scale};
        const ImVec2 corner{min.x + (size - extent.x) * 0.5f, min.y + (size - extent.y) * 0.5f};
        drawList->AddImage(ToImTextureId(*picture), corner, {corner.x + extent.x, corner.y + extent.y});
    } else {
        const float inset = item.subAsset ? size * 0.12f : 0.0f;
        const ImVec2 corner{min.x + inset, min.y + inset};
        const float side = size - inset * 2.0f;
        if (item.folder) {
            AssetIcons::DrawFolder(drawList, corner, side, item.hasContents);
        } else if (code) {
            AssetIcons::DrawFile(drawList, corner, side, item.name);
        } else {
            AssetIcons::DrawAsset(drawList, corner, side, item.type);
        }
    }

    // Files with parts have an arrow that shows them after the file.
    if (item.hasSubAssets && m_search.empty()) {
        const bool expanded = m_expanded.contains(item.id);
        const float button = ImGui::GetFrameHeight();
        ImGui::SetCursorScreenPos({max.x - button, min.y + (size - button) * 0.5f});
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        if (ImGui::Button(expanded ? ICON_FA_CHEVRON_LEFT : ICON_FA_CHEVRON_RIGHT, {button, button})) {
            if (expanded) {
                m_expanded.erase(item.id);
            } else {
                m_expanded.insert(item.id);
            }
            m_dirty = true;
        }
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip(expanded ? "Hide the parts of the file" : "Show the parts of the file");
    }

    const ImVec2 labelMin{min.x + style.ItemInnerSpacing.x, min.y + size + style.ItemInnerSpacing.y};
    const float labelWidth = size - style.ItemInnerSpacing.x * 2.0f;
    if (!item.folder) {
        // Under the icon, a line in the type's color, fading out toward the sides, and a caption naming the type
        // tell assets apart even when their pictures look alike, such as a material and a sphere mesh.
        const ImVec4 color =
            code ? AssetIcons::GetFileColor(GetFileKind(item.name)) : AssetIcons::GetTypeColor(item.type);
        const ImU32 on = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.9f));
        const ImU32 off = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.0f));
        const float lineTop = min.y + size;
        const float middle = (min.x + max.x) * 0.5f;
        drawList->AddRectFilledMultiColor({min.x + 6.0f, lineTop}, {middle, lineTop + 2.0f}, off, on, on, off);
        drawList->AddRectFilledMultiColor({middle, lineTop}, {max.x - 6.0f, lineTop + 2.0f}, on, off, off, on);

        ImGui::PushFont(nullptr, captionSize);
        const std::string caption = EllipsizeEnd(type, labelWidth);
        const float width = ImGui::CalcTextSize(caption.c_str()).x;
        drawList->AddText(
            {labelMin.x + (labelWidth - width) * 0.5f, labelMin.y + labelHeight + style.ItemInnerSpacing.y},
            ImGui::GetColorU32(ImGuiCol_TextDisabled), caption.c_str());
        ImGui::PopFont();
    }
    if (m_renaming == key) {
        ImGui::SetCursorScreenPos({min.x, labelMin.y - style.FramePadding.y});
        ImGui::SetNextItemWidth(size);
        if (std::exchange(m_renameFocus, false)) {
            ImGui::SetKeyboardFocusHere();
        }
        if (m_renameError) {
            ImGui::PushStyleColor(ImGuiCol_Border, EditorColors::Error);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        }
        const bool entered = ImGui::InputText("##Rename", &m_renameText,
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (m_renameError) {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%s", m_renameError->c_str());
        }
        if (ImGui::IsItemEdited()) {
            m_renameError.reset();
        }
        if (entered) {
            CommitRename(project);
        } else if (ImGui::IsItemDeactivated()) {
            // Escape, or a click elsewhere, leaves the name as it was unless it was confirmed.
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || m_renameError) {
                m_renaming.clear();
                m_renameError.reset();
            } else {
                CommitRename(project);
            }
        }
    } else {
        const std::string label = EllipsizeEnd(item.GetDisplayName(), labelWidth);
        const float width = ImGui::CalcTextSize(label.c_str()).x;
        drawList->AddText(
            {labelMin.x + (labelWidth - width) * 0.5f, labelMin.y},
            ImGui::GetColorU32(item.subAsset ? style.Colors[ImGuiCol_TextDisabled] : style.Colors[ImGuiCol_Text]),
            label.c_str());
    }
    // Back to where the next tile goes, after this one.
    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(tileSize);
    ImGui::PopID();
}

void AssetBrowserPanel::DrawItemMenu(EditorProject& project) {
    if (!ImGui::BeginPopup("##Item")) {
        return;
    }
    const std::vector<const BrowserItem*> items = GetSelectedItems();
    const BrowserItem* single = items.size() == 1 ? items.front() : nullptr;
    const bool hasFiles = std::ranges::any_of(items, [](const BrowserItem* item) { return !item->subAsset; });
    const bool onlyFiles = !items.empty() && std::ranges::all_of(items, [](const BrowserItem* item) {
        return !item->folder && !item->subAsset;
    });
    const bool onlyAssetFiles = onlyFiles && std::ranges::none_of(items, [](const BrowserItem* item) {
                                    return item->root == BrowserRoot::Code;
                                });

    if (single && ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open", "Enter")) {
        Open(project, *single);
    }
    if (ImGui::BeginMenu(ICON_FA_PLUS "  Create")) {
        DrawCreateItems(project);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_PEN "  Rename", "F2", false, single && !single->subAsset)) {
        BeginRename(*single);
    }
    if (ImGui::MenuItem(ICON_FA_CLONE "  Duplicate", "Ctrl+D", false, onlyFiles)) {
        DuplicateSelection(project);
    }
    if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Delete", "Delete", false, hasFiles)) {
        m_toDelete = GetSelectedPaths();
        m_openDeletePrompt = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_ROTATE "  Reimport", nullptr, false, onlyAssetFiles)) {
        for (const BrowserItem* item : items) {
            project.Reimport(item->id);
        }
    }
    if (single && ImGui::MenuItem(ICON_FA_UP_RIGHT_FROM_SQUARE "  Show in Explorer")) {
        const std::filesystem::path path = GetRootDirectory(project, single->root) / Utf8ToPath(single->path);
        Shell::Open(single->folder ? path : path.parent_path());
    }
    if (single && ImGui::MenuItem(ICON_FA_COPY "  Copy Path")) {
        Clipboard::SetText(single->subAsset ? single->path + "#" + single->name
                                            : GetShownPath(single->root, single->path));
    }
    if (single && !single->folder && single->root == BrowserRoot::Assets &&
        ImGui::MenuItem(ICON_FA_FINGERPRINT "  Copy Id")) {
        Clipboard::SetText(single->id.ToString());
    }
    ImGui::EndPopup();
}

void AssetBrowserPanel::DrawDeletePrompt(EditorProject& project) {
    constexpr const char* PromptId = "Delete Assets?###DeleteAssets";
    if (std::exchange(m_openDeletePrompt, false) && !m_toDelete.empty()) {
        ImGui::OpenPopup(PromptId);
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    if (!ImGui::BeginPopupModal(PromptId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextUnformatted(m_toDelete.size() == 1
                               ? std::format("Delete '{}'?", GetFileName(m_toDelete.front().path)).c_str()
                               : std::format("Delete {} items?", m_toDelete.size()).c_str());
    for (std::size_t i = 0; i < std::min<std::size_t>(m_toDelete.size(), 8) && m_toDelete.size() > 1; ++i) {
        ImGui::BulletText("%s", GetShownPath(m_toDelete[i].root, m_toDelete[i].path).c_str());
    }
    if (m_toDelete.size() > 8) {
        ImGui::TextDisabled("and %zu more", m_toDelete.size() - 8);
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
    ImGui::TextColored(EditorColors::TextMuted,
                       "Folders go with everything in them. This cannot be undone, and what refers to these assets "
                       "will show them as missing.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const float width = ImGui::GetFontSize() * 7.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.84f, 0.27f, 0.27f, 1.0f));
    if (ImGui::Button("Delete", {width, 0.0f})) {
        DeleteConfirmed(project);
        ImGui::CloseCurrentPopup();
    }
    m_rects["DeletePrompt/Delete"] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    if (ImGui::Button("Cancel", {width, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_toDelete.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AssetBrowserPanel::DrawComponentDialog(EditorProject& project) {
    constexpr const char* DialogId = "New C++ Component###NewComponent";
    if (std::exchange(m_openComponentDialog, false)) {
        ImGui::OpenPopup(DialogId);
        m_componentName = "NewComponent";
        m_componentFocus = true;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    if (!ImGui::BeginPopupModal(DialogId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    // Made in the code folder shown, or else at the root of the code.
    const std::string folder = m_root == BrowserRoot::Code ? m_folder : std::string();
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
    if (std::exchange(m_componentFocus, false)) {
        ImGui::SetKeyboardFocusHere();
    }
    const bool entered = ImGui::InputText("##Name", &m_componentName,
                                          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    m_rects["NewComponent/Name"] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    const std::string problem = ValidateComponentName(project.GetProject(), folder, m_componentName);
    if (!problem.empty()) {
        ImGui::TextColored(EditorColors::Error, "%s", problem.c_str());
    } else {
        const std::string where =
            GetShownPath(BrowserRoot::Code, folder.empty() ? m_componentName : folder + "/" + m_componentName);
        ImGui::TextColored(EditorColors::TextMuted, "Writes %s.h and %s.cpp", where.c_str(), where.c_str());
    }
    ImGui::Spacing();

    const float width = ImGui::GetFontSize() * 7.0f;
    ImGui::BeginDisabled(!problem.empty());
    const bool create = ImGui::Button("Create", {width, 0.0f});
    ImGui::EndDisabled();
    m_rects["NewComponent/Create"] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel", {width, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if ((create || entered) && problem.empty()) {
        CreateComponent(project, folder, m_componentName);
        ImGui::CloseCurrentPopup();
    } else if (cancel) {
        ImGui::CloseCurrentPopup();
    } else if (entered) {
        // A name that cannot be keeps the field for fixing it.
        m_componentFocus = true;
    }
    ImGui::EndPopup();
}

void AssetBrowserPanel::CreateComponent(EditorProject& project, const std::string& folder, const std::string& name) {
    const auto files = CreateComponentCode(project.GetProject(), folder, name);
    if (!files) {
        return;
    }
    SetFolder(BrowserRoot::Code, folder);
    std::vector<Location> select;
    for (const std::string& file : *files) {
        select.push_back({BrowserRoot::Code, file});
    }
    AfterChange(project, select);
}

void AssetBrowserPanel::HandleKeyboard(EditorProject& project) {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput ||
        !m_renaming.empty() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        return;
    }
    const std::vector<const BrowserItem*> selected = GetSelectedItems();
    if (ImGui::Shortcut(ImGuiKey_Delete) && !GetSelectedPaths().empty()) {
        m_toDelete = GetSelectedPaths();
        m_openDeletePrompt = true;
    }
    if (ImGui::Shortcut(ImGuiKey_F2) && selected.size() == 1 && !selected.front()->subAsset) {
        BeginRename(*selected.front());
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D)) {
        DuplicateSelection(project);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A)) {
        m_selection.clear();
        for (const BrowserItem& item : m_items) {
            m_selection.push_back(item.GetKey());
        }
    }
    if (ImGui::Shortcut(ImGuiKey_Enter) && selected.size() == 1) {
        Open(project, *selected.front());
    }
    if (ImGui::Shortcut(ImGuiKey_Backspace) && m_search.empty() && !m_folder.empty()) {
        const std::string child = m_folder;
        SetFolder(m_root, GetParentFolder(m_folder));
        m_selection = {BrowserItem::GetFolderKey(m_root, child)};
        m_anchor = m_selection.front();
        m_scrollTo = m_anchor;
    }

    // Arrows move the selection through the grid; Shift extends it.
    if (m_items.empty()) {
        return;
    }
    const auto anchor = std::ranges::find_if(m_items, [&](const BrowserItem& item) {
        return item.GetKey() == (m_selection.empty() ? std::string() : m_selection.back());
    });
    const int current = anchor != m_items.end() ? static_cast<int>(anchor - m_items.begin()) : -1;
    int step = 0;
    if (ImGui::Shortcut(ImGuiKey_RightArrow, ImGuiInputFlags_Repeat) ||
        ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_RightArrow, ImGuiInputFlags_Repeat)) {
        step = 1;
    } else if (ImGui::Shortcut(ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat) ||
               ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat)) {
        step = -1;
    } else if (ImGui::Shortcut(ImGuiKey_DownArrow, ImGuiInputFlags_Repeat) ||
               ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_DownArrow, ImGuiInputFlags_Repeat)) {
        step = m_columns;
    } else if (ImGui::Shortcut(ImGuiKey_UpArrow, ImGuiInputFlags_Repeat) ||
               ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_UpArrow, ImGuiInputFlags_Repeat)) {
        step = -m_columns;
    }
    if (step == 0) {
        return;
    }
    const int last = static_cast<int>(m_items.size()) - 1;
    const int next = current < 0 ? 0 : std::clamp(current + step, 0, last);
    const std::string key = m_items[static_cast<std::size_t>(next)].GetKey();
    if (ImGui::GetIO().KeyShift && !m_anchor.empty()) {
        m_selection.erase(std::remove(m_selection.begin(), m_selection.end(), key), m_selection.end());
        m_selection.push_back(key);
    } else {
        m_selection = {key};
        m_anchor = key;
    }
    m_scrollTo = key;
}

void AssetBrowserPanel::HandleDrops(EditorProject& project) {
    if (m_drops.empty()) {
        return;
    }
    // Files dropped together arrive one by one; they are copied together, into the folder under the first.
    BrowserRoot root = m_root;
    std::string folder = m_folder;
    const ImVec2 position = m_drops.front().second;
    for (const Tile& tile : m_tiles) {
        if (!tile.folder.empty() && ImRect(tile.min, tile.max).Contains(position)) {
            root = tile.root;
            folder = tile.folder;
        }
    }
    std::vector<std::filesystem::path> files;
    for (auto& [file, at] : std::exchange(m_drops, {})) {
        files.push_back(std::move(file));
    }
    Import(project, files, root, folder);
}

void AssetBrowserPanel::AcceptMoves(EditorProject& project, BrowserRoot root, const std::string& folder) {
    if (!ImGui::BeginDragDropTarget()) {
        return;
    }
    std::vector<std::string> paths;
    if (root == BrowserRoot::Code) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(CodePathsPayload)) {
            paths = GetPayloadPaths(*payload);
        }
    } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetsPayload)) {
        for (const AssetId id : GetPayloadAssets(*payload)) {
            const AssetInfo* info = project.GetAssetDatabase().Find(id);
            if (info && !info->parent.IsValid()) {
                paths.push_back(info->path);
            }
        }
    } else if (const ImGuiPayload* pathsPayload = ImGui::AcceptDragDropPayload(AssetPathsPayload)) {
        paths = GetPayloadPaths(*pathsPayload);
    }
    ImGui::EndDragDropTarget();
    if (!paths.empty()) {
        m_pendingMove = PendingMove{root, std::move(paths), folder};
    }
}

void AssetBrowserPanel::Select(const BrowserItem& item) {
    const std::string key = item.GetKey();
    const ImGuiIO& io = ImGui::GetIO();
    // The inspector shows the asset clicked last.
    if (!item.folder && item.root == BrowserRoot::Assets) {
        m_context.InspectAsset(item.id);
    }
    if (io.KeyCtrl) {
        const auto found = std::ranges::find(m_selection, key);
        if (found != m_selection.end()) {
            m_selection.erase(found);
        } else {
            m_selection.push_back(key);
        }
        m_anchor = key;
        return;
    }
    if (io.KeyShift && !m_anchor.empty()) {
        // The items between the anchor and this one, in the order shown.
        const auto from =
            std::ranges::find_if(m_items, [&](const BrowserItem& each) { return each.GetKey() == m_anchor; });
        const auto to = std::ranges::find_if(m_items, [&](const BrowserItem& each) { return each.GetKey() == key; });
        if (from != m_items.end() && to != m_items.end()) {
            m_selection.clear();
            const auto [first, last] = std::minmax(from, to);
            for (auto it = first; it <= last; ++it) {
                m_selection.push_back(it->GetKey());
            }
            // The clicked one is primary.
            std::erase(m_selection, key);
            m_selection.push_back(key);
            return;
        }
    }
    m_selection = {key};
    m_anchor = key;
}

void AssetBrowserPanel::Open(EditorProject& project, const BrowserItem& item) {
    if (item.folder) {
        SetFolder(item.root, item.path);
        return;
    }
    if (item.root == BrowserRoot::Assets && item.type == AssetType::Scene) {
        if (SceneWorkspace* workspace = m_context.GetWorkspace()) {
            workspace->OpenScene(item.id);
        }
        return;
    }
    if (item.hasSubAssets) {
        if (!m_expanded.erase(item.id)) {
            m_expanded.insert(item.id);
        }
        m_dirty = true;
        return;
    }
    // Others open in the application the OS opens them with, such as an image editor, or an IDE for code.
    Shell::Open(GetRootDirectory(project, item.root) / Utf8ToPath(item.path));
}

void AssetBrowserPanel::BeginRename(const BrowserItem& item) {
    if (item.subAsset) {
        return;
    }
    m_renaming = item.GetKey();
    // Files keep their extension, which says what kind of file they are.
    const std::string extension = item.folder ? std::string() : GetExtension(item.name);
    m_renameText = item.name.substr(0, item.name.size() - extension.size());
    m_renameError.reset();
    m_renameFocus = true;
    m_selection = {m_renaming};
    m_anchor = m_renaming;
    m_scrollTo = m_renaming;
}

void AssetBrowserPanel::CommitRename(EditorProject& project) {
    const BrowserItem* item = FindItem(m_renaming);
    if (!item) {
        m_renaming.clear();
        return;
    }
    const std::string name = m_renameText + (item->folder ? std::string() : GetExtension(item->name));
    if (name == item->name) {
        m_renaming.clear();
        return;
    }
    if (const auto error = ValidateFileName(m_renameText.empty() ? std::string() : name)) {
        m_renameError = error;
        m_renameFocus = true;
        return;
    }
    const std::string path = JoinPath(GetParentFolder(item->path), name);
    std::error_code error;
    const std::filesystem::path directory = GetRootDirectory(project, item->root);
    if (std::filesystem::exists(directory / Utf8ToPath(path), error) &&
        !std::filesystem::equivalent(directory / Utf8ToPath(path), directory / Utf8ToPath(item->path), error)) {
        m_renameError = "Something in this folder already has that name.";
        m_renameFocus = true;
        return;
    }
    const BrowserRoot root = item->root;
    const std::string from = item->path;
    m_renaming.clear();
    m_renameError.reset();
    if (WithFiles(project, root, [&](auto& files) { return files.Move(from, path); })) {
        AfterChange(project, {{root, path}});
    }
}

void AssetBrowserPanel::Create(EditorProject& project, NewAsset kind) {
    if (kind != NewAsset::Folder && m_root != BrowserRoot::Assets) {
        return;
    }
    const std::filesystem::path assets = project.GetProject().GetAssetsDirectory();
    std::string path;
    bool created = false;
    switch (kind) {
    case NewAsset::Folder:
        created = WithFiles(project, m_root, [&](auto& files) {
            path = files.GetFreePath(JoinPath(m_folder, "New Folder"));
            return files.CreateFolder(path);
        });
        break;
    case NewAsset::Scene: {
        path = project.GetAssetDatabase().GetFreePath(JoinPath(m_folder, "New Scene.scene"));
        Scene scene;
        PopulateNewScene(scene);
        created = scene.SaveToFile(assets / Utf8ToPath(path));
        break;
    }
    case NewAsset::Material:
        path = project.GetAssetDatabase().GetFreePath(JoinPath(m_folder, "New Material.material"));
        created = MaterialSource{}.SaveToFile(assets / Utf8ToPath(path));
        break;
    }
    if (!created) {
        GetEditorLogger().Error("Could not create '{}'", path);
        return;
    }
    m_search.clear();
    AfterChange(project, {{m_root, path}});
    // New items are named right away.
    if (!m_selection.empty()) {
        if (const BrowserItem* item = FindItem(m_selection.front())) {
            BeginRename(*item);
        }
    }
}

void AssetBrowserPanel::DuplicateSelection(EditorProject& project) {
    std::vector<Location> copies;
    for (const BrowserItem* item : GetSelectedItems()) {
        if (item->folder || item->subAsset) {
            continue;
        }
        std::string copy;
        if (WithFiles(project, item->root, [&](auto& files) {
                copy = files.GetFreePath(item->path);
                return files.Duplicate(item->path, copy);
            })) {
            copies.push_back({item->root, copy});
        }
    }
    if (!copies.empty()) {
        AfterChange(project, copies);
    }
}

void AssetBrowserPanel::DeleteConfirmed(EditorProject& project) {
    for (const Location& location : std::exchange(m_toDelete, {})) {
        WithFiles(project, location.root, [&](auto& files) { return files.Delete(location.path); });
    }
    m_selection.clear();
    AfterChange(project, {});
}

void AssetBrowserPanel::Move(EditorProject& project, BrowserRoot root, const std::vector<std::string>& paths,
                             const std::string& folder) {
    std::vector<Location> moved;
    WithFiles(project, root, [&](auto& files) {
        for (const std::string& path : paths) {
            // Already there, or a folder into itself.
            if (GetParentFolder(path) == folder || IsInFolder(folder, path)) {
                continue;
            }
            const std::string target = files.GetFreePath(JoinPath(folder, GetFileName(path)));
            if (files.Move(path, target)) {
                moved.push_back({root, target});
            }
        }
        return true;
    });
    if (!moved.empty()) {
        AfterChange(project, root == m_root && folder == m_folder ? moved : std::vector<Location>());
    }
}

void AssetBrowserPanel::Import(EditorProject& project, std::span<const std::filesystem::path> files, BrowserRoot root,
                               const std::string& folder) {
    const std::vector<std::string> copied =
        WithFiles(project, root, [&](auto& target) { return target.CopyIn(files, folder); });
    if (copied.empty()) {
        return;
    }
    const std::string directory = PathToUtf8(GetRootDirectory(project, root).filename().u8string());
    const std::string where = folder.empty() ? directory : directory + "/" + folder;
    GetEditorLogger().Info(
        "Copied {} into '{}'",
        copied.size() == 1 ? "'" + GetFileName(copied.front()) + "'" : std::format("{} files", copied.size()), where);
    SetFolder(root, folder);
    std::vector<Location> select;
    for (const std::string& path : copied) {
        select.push_back({root, path});
    }
    AfterChange(project, select);
}

void AssetBrowserPanel::BrowseForImport() {
    FileDialog::OpenFile(
        &m_context.GetWindow(), ImportFilters,
        [self = std::weak_ptr(m_self)](std::vector<std::filesystem::path> paths) {
            const auto panel = self.lock();
            if (!panel || !*panel || paths.empty()) {
                return;
            }
            AssetBrowserPanel& owner = **panel;
            if (EditorProject* project = owner.m_context.GetProject()) {
                owner.Import(*project, paths, owner.m_root, owner.m_folder);
            }
        },
        true, GetUserFolder(UserFolder::Downloads));
}

void AssetBrowserPanel::AfterChange(EditorProject& project, const std::vector<Location>& select) {
    project.Refresh();
    Rebuild(project);
    std::vector<std::string> keys;
    for (const Location& location : select) {
        std::error_code error;
        const std::filesystem::path path = GetRootDirectory(project, location.root) / Utf8ToPath(location.path);
        if (std::filesystem::is_directory(path, error) || location.root == BrowserRoot::Code) {
            keys.push_back(location.root == BrowserRoot::Code
                               ? "code:" + location.path
                               : BrowserItem::GetFolderKey(location.root, location.path));
        } else if (const auto id = project.GetAssetDatabase().FindByPath(location.path)) {
            keys.push_back(id->ToString());
        }
    }
    if (!keys.empty()) {
        m_selection = keys;
        m_anchor = keys.back();
        m_scrollTo = keys.front();
    }
}

} // namespace Spot::Editor
