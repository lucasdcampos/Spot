#include "panels/asset_browser_panel.h"

#include "assets/asset_list.h"
#include "assets/thumbnail_cache.h"
#include "core/editor_log.h"
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
constexpr ImVec4 FolderColor{0.886f, 0.725f, 0.380f, 1.0f};

const std::array ImportFilters{
    FileFilter{"Assets", "png;jpg;jpeg;bmp;tga;gltf;glb;bin;wav;ogg;ttf;otf;material;scene"},
    FileFilter{"All files", "*"},
};

ImVec4 GetTypeColor(AssetType type) {
    switch (type) {
    case AssetType::Texture:
        return {0.475f, 0.765f, 0.490f, 1.0f};
    case AssetType::Mesh:
        return {0.553f, 0.733f, 0.886f, 1.0f};
    case AssetType::Material:
        return {0.906f, 0.533f, 0.420f, 1.0f};
    case AssetType::Model:
        return {0.412f, 0.588f, 0.941f, 1.0f};
    case AssetType::Sound:
        return {0.906f, 0.776f, 0.388f, 1.0f};
    case AssetType::Font:
        return {0.800f, 0.800f, 0.820f, 1.0f};
    case AssetType::Scene:
        return EditorColors::Accent;
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

const char* GetItemIcon(const BrowserItem& item) {
    return item.folder ? ICON_FA_FOLDER : GetAssetTypeIcon(item.type);
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

void AssetBrowserPanel::SetFolder(std::string folder) {
    if (folder == m_folder && m_search.empty()) {
        return;
    }
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

std::vector<std::string> AssetBrowserPanel::GetSelectedPaths() const {
    std::vector<std::string> paths;
    for (const BrowserItem* item : GetSelectedItems()) {
        if (!item->subAsset) {
            paths.push_back(item->path);
        }
    }
    return paths;
}

void AssetBrowserPanel::Rebuild(EditorProject& project) {
    m_builtVersion = project.GetVersion();
    m_dirty = false;
    const AssetDatabase& database = project.GetAssetDatabase();
    m_folders = ListFolders(project.GetProject().GetAssetsDirectory());
    // A folder that went away, such as one deleted outside the editor, leaves its nearest parent shown.
    while (!m_folder.empty() && std::ranges::find(m_folders, m_folder) == m_folders.end()) {
        m_folder = GetParentFolder(m_folder);
    }

    if (!m_search.empty()) {
        m_items = SearchAssets(database, m_folders, m_search);
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
    if (m_dirty || m_builtVersion != project->GetVersion()) {
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
    ImGui::SetItemTooltip("Copy files into this folder as assets; dropping them from the OS does it too");
    ImGui::SameLine();
    DrawBreadcrumbs(project);

    // The search and the tile size, on the right.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float searchWidth = ImGui::GetFontSize() * 12.0f;
    const float sizeWidth = ImGui::GetFontSize() * 5.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - searchWidth - sizeWidth - style.ItemSpacing.x));
    ImGui::SetNextItemWidth(searchWidth);
    if (ImGui::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS "  Search all assets", &m_search)) {
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
    ImGui::Separator();
    item(ICON_FA_MOUNTAIN_SUN, "Scene", NewAsset::Scene);
    item(ICON_FA_PALETTE, "Material", NewAsset::Material);
}

void AssetBrowserPanel::DrawBreadcrumbs(EditorProject& project) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    std::string path;
    const auto crumb = [&](const char* label, const std::string& folder, bool last) {
        ImGui::PushID(folder.c_str());
        if (ImGui::Button(label) && !last) {
            SetFolder(folder);
        }
        AcceptMoves(project, folder);
        ImGui::PopID();
    };
    const bool searching = !m_search.empty();
    crumb(ICON_FA_FOLDER_OPEN "  Assets", "", m_folder.empty() && !searching);
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
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (m_folder.empty() && m_search.empty()) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    const bool open = ImGui::TreeNodeEx("##Root", flags, "%s", ICON_FA_FOLDER_OPEN "  Assets");
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        SetFolder({});
    }
    AcceptMoves(project, {});
    if (open) {
        for (const std::string& folder : GetSubfolders(m_folders, {})) {
            DrawFolderNode(project, folder);
        }
        ImGui::TreePop();
    }
    m_revealFolder = false;
}

void AssetBrowserPanel::DrawFolderNode(EditorProject& project, const std::string& folder) {
    const std::vector<std::string> subfolders = GetSubfolders(m_folders, folder);
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (subfolders.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (folder == m_folder && m_search.empty()) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    // The folder shown is revealed when it changes, by opening the folders above it.
    if (m_revealFolder && IsInFolder(m_folder, folder) && folder != m_folder) {
        ImGui::SetNextItemOpen(true);
    }
    // The node spans the row and has no label of its own, so the icon can have the color of folders.
    const bool open = ImGui::TreeNodeEx(folder.c_str(), flags, "%s", "");
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        SetFolder(folder);
    }
    AcceptMoves(project, folder);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(FolderColor, ICON_FA_FOLDER);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(GetFileName(folder).c_str());
    if (open) {
        for (const std::string& subfolder : subfolders) {
            DrawFolderNode(project, subfolder);
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
        DrawEmptyState(m_search.empty() ? ICON_FA_FOLDER_OPEN : ICON_FA_MAGNIFYING_GLASS,
                       m_search.empty() ? "This folder is empty. Drop files here, or right-click to create assets."
                                        : "No asset matches the search.");
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
            Shell::Open(project.GetProject().GetAssetsDirectory() / Utf8ToPath(m_folder));
        }
        if (ImGui::MenuItem(ICON_FA_ROTATE "  Refresh")) {
            project.Refresh();
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
    const ImVec2 tileSize{size, size + labelHeight + style.ItemInnerSpacing.y * 2.0f};
    const bool selected = std::ranges::find(m_selection, key) != m_selection.end();

    ImGui::PushID(key.c_str());
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##Tile", tileSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    m_tiles.push_back({key, item.folder ? item.path : std::string(), min, max});
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
    if (hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImGui::SetItemTooltip("%s\n%s", item.subAsset ? (item.path + " #" + item.name).c_str() : item.path.c_str(),
                              item.folder ? "Folder" : std::string(GetAssetTypeName(item.type)).c_str());
    }

    // Dragged assets go to fields, scenes and folders; folders only to folders.
    if (ImGui::BeginDragDropSource()) {
        std::vector<const BrowserItem*> dragged = selected ? GetSelectedItems() : std::vector{&item};
        const bool onlyAssets = std::ranges::none_of(dragged, &BrowserItem::folder);
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
            ImGui::SetDragDropPayload(AssetPathsPayload, paths.data(), paths.size());
        }
        if (dragged.size() == 1) {
            ImGui::Text("%s  %s", GetItemIcon(item), item.name.c_str());
        } else {
            ImGui::Text(ICON_FA_LAYER_GROUP "  %zu items", dragged.size());
        }
        ImGui::EndDragDropSource();
    }
    if (item.folder) {
        AcceptMoves(project, item.path);
    }

    // Background, icon and name.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (selected || hovered) {
        const ImVec4 color = selected ? ImVec4(EditorColors::Accent.x, EditorColors::Accent.y, EditorColors::Accent.z,
                                               hovered ? 0.45f : 0.35f)
                                      : style.Colors[ImGuiCol_FrameBgHovered];
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(color), style.FrameRounding);
    }
    if (item.subAsset) {
        // Parts of a file sit on a darker card, so they read as belonging to the file before them.
        drawList->AddRectFilled({min.x + 2.0f, min.y + 2.0f}, {max.x - 2.0f, min.y + size - 2.0f},
                                ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
    }
    ThumbnailCache* thumbnails = m_context.GetThumbnails();
    const Texture* picture = thumbnails && !item.folder ? thumbnails->Get(project, item.id, item.type) : nullptr;
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
        const char* icon = GetItemIcon(item);
        const float iconSize = size * (item.subAsset ? 0.34f : 0.44f);
        ImFont* font = ImGui::GetFont();
        const ImVec2 iconExtent = font->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
        drawList->AddText(font, iconSize, {min.x + (size - iconExtent.x) * 0.5f, min.y + (size - iconExtent.y) * 0.5f},
                          ImGui::GetColorU32(item.folder ? FolderColor : GetTypeColor(item.type)), icon);
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
        const std::string label = EllipsizeEnd(item.name, labelWidth);
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
    if (ImGui::MenuItem(ICON_FA_ROTATE "  Reimport", nullptr, false, onlyFiles)) {
        for (const BrowserItem* item : items) {
            project.Reimport(item->id);
        }
    }
    if (single && ImGui::MenuItem(ICON_FA_UP_RIGHT_FROM_SQUARE "  Show in Explorer")) {
        const std::filesystem::path path = project.GetProject().GetAssetsDirectory() / Utf8ToPath(single->path);
        Shell::Open(single->folder ? path : path.parent_path());
    }
    if (single && ImGui::MenuItem(ICON_FA_COPY "  Copy Path")) {
        Clipboard::SetText(single->subAsset ? single->path + "#" + single->name : single->path);
    }
    if (single && !single->folder && ImGui::MenuItem(ICON_FA_FINGERPRINT "  Copy Id")) {
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
    ImGui::TextUnformatted(m_toDelete.size() == 1 ? std::format("Delete '{}'?", GetFileName(m_toDelete.front())).c_str()
                                                  : std::format("Delete {} items?", m_toDelete.size()).c_str());
    for (std::size_t i = 0; i < std::min<std::size_t>(m_toDelete.size(), 8) && m_toDelete.size() > 1; ++i) {
        ImGui::BulletText("%s", m_toDelete[i].c_str());
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
        SetFolder(GetParentFolder(m_folder));
        m_selection = {"folder:" + child};
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
    // Files dropped together arrive one by one; they are imported together, into the folder under the first.
    std::string folder = m_folder;
    const ImVec2 position = m_drops.front().second;
    for (const Tile& tile : m_tiles) {
        if (!tile.folder.empty() && ImRect(tile.min, tile.max).Contains(position)) {
            folder = tile.folder;
        }
    }
    std::vector<std::filesystem::path> files;
    for (auto& [file, at] : std::exchange(m_drops, {})) {
        files.push_back(std::move(file));
    }
    Import(project, files, folder);
}

void AssetBrowserPanel::AcceptMoves(EditorProject& project, const std::string& folder) {
    if (!ImGui::BeginDragDropTarget()) {
        return;
    }
    std::vector<std::string> paths;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetsPayload)) {
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
        Move(project, paths, folder);
    }
}

void AssetBrowserPanel::Select(const BrowserItem& item) {
    const std::string key = item.GetKey();
    const ImGuiIO& io = ImGui::GetIO();
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
        SetFolder(item.path);
        return;
    }
    if (item.type == AssetType::Scene) {
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
    // Others open in the application the OS opens them with, such as an image editor.
    Shell::Open(project.GetProject().GetAssetsDirectory() / Utf8ToPath(item.path));
}

void AssetBrowserPanel::BeginRename(const BrowserItem& item) {
    if (item.subAsset) {
        return;
    }
    m_renaming = item.GetKey();
    // Files keep their extension, which says what kind of asset they are.
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
    const std::filesystem::path assets = project.GetProject().GetAssetsDirectory();
    if (std::filesystem::exists(assets / Utf8ToPath(path), error) &&
        !std::filesystem::equivalent(assets / Utf8ToPath(path), assets / Utf8ToPath(item->path), error)) {
        m_renameError = "Something in this folder already has that name.";
        m_renameFocus = true;
        return;
    }
    const std::string from = item->path;
    m_renaming.clear();
    m_renameError.reset();
    if (project.GetAssetDatabase().Move(from, path)) {
        AfterChange(project, {path});
    }
}

void AssetBrowserPanel::Create(EditorProject& project, NewAsset kind) {
    AssetDatabase& database = project.GetAssetDatabase();
    const std::filesystem::path assets = project.GetProject().GetAssetsDirectory();
    std::string path;
    bool created = false;
    switch (kind) {
    case NewAsset::Folder:
        path = database.GetFreePath(JoinPath(m_folder, "New Folder"));
        created = database.CreateFolder(path);
        break;
    case NewAsset::Scene: {
        path = database.GetFreePath(JoinPath(m_folder, "New Scene.scene"));
        Scene scene;
        PopulateNewScene(scene);
        created = scene.SaveToFile(assets / Utf8ToPath(path));
        break;
    }
    case NewAsset::Material:
        path = database.GetFreePath(JoinPath(m_folder, "New Material.material"));
        created = MaterialSource{}.SaveToFile(assets / Utf8ToPath(path));
        break;
    }
    if (!created) {
        GetEditorLogger().Error("Could not create '{}'", path);
        return;
    }
    m_search.clear();
    AfterChange(project, {path});
    // New assets are named right away.
    if (!m_selection.empty()) {
        if (const BrowserItem* item = FindItem(m_selection.front())) {
            BeginRename(*item);
        }
    }
}

void AssetBrowserPanel::DuplicateSelection(EditorProject& project) {
    AssetDatabase& database = project.GetAssetDatabase();
    std::vector<std::string> copies;
    for (const BrowserItem* item : GetSelectedItems()) {
        if (item->folder || item->subAsset) {
            continue;
        }
        const std::string copy = database.GetFreePath(item->path);
        if (database.Duplicate(item->path, copy)) {
            copies.push_back(copy);
        }
    }
    if (!copies.empty()) {
        AfterChange(project, copies);
    }
}

void AssetBrowserPanel::DeleteConfirmed(EditorProject& project) {
    AssetDatabase& database = project.GetAssetDatabase();
    for (const std::string& path : std::exchange(m_toDelete, {})) {
        database.Delete(path);
    }
    m_selection.clear();
    AfterChange(project, {});
}

void AssetBrowserPanel::Move(EditorProject& project, const std::vector<std::string>& paths, const std::string& folder) {
    AssetDatabase& database = project.GetAssetDatabase();
    std::vector<std::string> moved;
    for (const std::string& path : paths) {
        // Already there, or a folder into itself.
        if (GetParentFolder(path) == folder || IsInFolder(folder, path)) {
            continue;
        }
        const std::string target = database.GetFreePath(JoinPath(folder, GetFileName(path)));
        if (database.Move(path, target)) {
            moved.push_back(target);
        }
    }
    if (!moved.empty()) {
        AfterChange(project, folder == m_folder ? moved : std::vector<std::string>());
    }
}

void AssetBrowserPanel::Import(EditorProject& project, std::span<const std::filesystem::path> files,
                               const std::string& folder) {
    const std::vector<std::string> copied = project.GetAssetDatabase().CopyIn(files, folder);
    if (copied.empty()) {
        return;
    }
    GetEditorLogger().Info("Imported {} into '{}'",
                           copied.size() == 1 ? "'" + GetFileName(copied.front()) + "'"
                                              : std::format("{} files", copied.size()),
                           folder.empty() ? std::string("Assets") : folder);
    SetFolder(folder);
    AfterChange(project, copied);
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
                owner.Import(*project, paths, owner.m_folder);
            }
        },
        true, GetUserFolder(UserFolder::Downloads));
}

void AssetBrowserPanel::AfterChange(EditorProject& project, const std::vector<std::string>& select) {
    project.Refresh();
    Rebuild(project);
    std::vector<std::string> keys;
    const std::filesystem::path assets = project.GetProject().GetAssetsDirectory();
    for (const std::string& path : select) {
        std::error_code error;
        if (std::filesystem::is_directory(assets / Utf8ToPath(path), error)) {
            keys.push_back("folder:" + path);
        } else if (const auto id = project.GetAssetDatabase().FindByPath(path)) {
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
