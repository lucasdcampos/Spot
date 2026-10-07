#include "assets/asset_browser_model.h"

#include "core/text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>
#include <unordered_set>

namespace Spot::Editor {

namespace {

std::string ToLower(std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return lower;
}

void SortByName(std::vector<BrowserItem>& items) {
    std::ranges::stable_sort(items, [](const BrowserItem& a, const BrowserItem& b) {
        if (a.folder != b.folder) {
            return a.folder;
        }
        return ToLower(a.name) < ToLower(b.name);
    });
}

BrowserItem MakeFolderItem(const std::string& path) {
    return {.path = path, .name = GetFileName(path), .folder = true};
}

// The files that have sub-assets.
std::unordered_set<AssetId> GetParents(const AssetDatabase& database) {
    std::unordered_set<AssetId> parents;
    for (const AssetInfo& info : database.GetAssets()) {
        if (info.parent.IsValid()) {
            parents.insert(info.parent);
        }
    }
    return parents;
}

BrowserItem MakeAssetItem(const AssetInfo& info, const std::unordered_set<AssetId>& parents) {
    BrowserItem item{.path = info.path, .id = info.id, .type = info.type, .subAsset = info.parent.IsValid()};
    item.name = item.subAsset ? info.name : GetFileName(info.path);
    item.hasSubAssets = parents.contains(info.id);
    return item;
}

} // namespace

std::string BrowserItem::GetKey() const {
    return folder ? "folder:" + path : id.ToString();
}

std::vector<std::string> ListFolders(const std::filesystem::path& assetsDirectory) {
    std::vector<std::string> folders;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(assetsDirectory, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (!it->is_directory(error)) {
            continue;
        }
        if (it->path().filename().u8string().starts_with(u8".")) {
            it.disable_recursion_pending();
            continue;
        }
        folders.push_back(PathToUtf8(it->path().lexically_relative(assetsDirectory).generic_u8string()));
    }
    // Tree order: a folder, then what is inside it, each level by name.
    std::ranges::sort(folders, [](const std::string& a, const std::string& b) {
        return std::ranges::lexicographical_compare(ToLower(a), ToLower(b), [](char x, char y) {
            // The separator comes before every other character, so "a/b" sorts right after "a".
            const auto rank = [](char c) { return c == '/' ? '\0' : c; };
            return rank(x) < rank(y);
        });
    });
    return folders;
}

std::vector<std::string> GetSubfolders(const std::vector<std::string>& folders, std::string_view folder) {
    std::vector<std::string> subfolders;
    for (const std::string& path : folders) {
        if (GetParentFolder(path) == folder) {
            subfolders.push_back(path);
        }
    }
    return subfolders;
}

std::vector<BrowserItem> ListFolder(const AssetDatabase& database, const std::vector<std::string>& folders,
                                    std::string_view folder) {
    std::vector<BrowserItem> items;
    for (const std::string& path : GetSubfolders(folders, folder)) {
        items.push_back(MakeFolderItem(path));
    }
    const std::unordered_set<AssetId> parents = GetParents(database);
    for (const AssetInfo& info : database.GetAssets()) {
        if (!info.parent.IsValid() && GetParentFolder(info.path) == folder) {
            items.push_back(MakeAssetItem(info, parents));
        }
    }
    SortByName(items);
    return items;
}

std::vector<BrowserItem> ListSubAssets(const AssetDatabase& database, AssetId file) {
    std::vector<BrowserItem> items;
    for (const AssetInfo& info : database.GetAssets()) {
        if (info.parent == file) {
            items.push_back(MakeAssetItem(info, {}));
        }
    }
    SortByName(items);
    return items;
}

std::vector<BrowserItem> SearchAssets(const AssetDatabase& database, const std::vector<std::string>& folders,
                                      std::string_view text) {
    std::vector<BrowserItem> items;
    for (const std::string& path : folders) {
        if (ContainsIgnoringCase(GetFileName(path), text)) {
            items.push_back(MakeFolderItem(path));
        }
    }
    const std::unordered_set<AssetId> parents = GetParents(database);
    for (const AssetInfo& info : database.GetAssets()) {
        BrowserItem item = MakeAssetItem(info, parents);
        if (ContainsIgnoringCase(item.name, text)) {
            items.push_back(std::move(item));
        }
    }
    SortByName(items);
    return items;
}

std::string GetParentFolder(std::string_view path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string_view::npos ? std::string() : std::string(path.substr(0, slash));
}

std::string GetFileName(std::string_view path) {
    const std::size_t slash = path.find_last_of('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

std::string JoinPath(std::string_view folder, std::string_view name) {
    return folder.empty() ? std::string(name) : std::string(folder) + "/" + std::string(name);
}

bool IsInFolder(std::string_view path, std::string_view folder) {
    if (folder.empty()) {
        return true;
    }
    return path.starts_with(folder) && (path.size() == folder.size() || path[folder.size()] == '/');
}

std::optional<std::string> ValidateFileName(std::string_view name) {
    if (name.empty() || name.find_first_not_of(' ') == std::string_view::npos) {
        return "The name is empty.";
    }
    if (name == "." || name == "..") {
        return "The name cannot be a dot or two.";
    }
    if (name.starts_with('.')) {
        return "Names that start with a dot are hidden from the assets.";
    }
    if (name.ends_with(' ') || name.ends_with('.')) {
        return "The name cannot end with a space or a dot.";
    }
    constexpr std::string_view Forbidden = "<>:\"/\\|?*";
    for (const char c : name) {
        if (static_cast<unsigned char>(c) < 32) {
            return "The name cannot have control characters.";
        }
        if (Forbidden.find(c) != std::string_view::npos) {
            return std::string("The name cannot have '") + c + "'.";
        }
    }
    // Names Windows keeps for devices, with or without an extension.
    static constexpr std::array Reserved{"con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4",
                                         "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
                                         "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    const std::string stem = ToLower(name.substr(0, name.find('.')));
    if (std::ranges::find(Reserved, stem) != Reserved.end()) {
        return "Windows keeps that name for a device.";
    }
    return std::nullopt;
}

} // namespace Spot::Editor
