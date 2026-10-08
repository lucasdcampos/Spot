#pragma once

#include <spot.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What the asset browser shows of a project: its assets, as folders, the assets of source files and their sub-assets,
// and its code, as plain folders and files. Paths are relative to their root's directory, with '/' separators; the
// directory itself is "".
namespace Spot::Editor {

// The folders the browser shows.
enum class BrowserRoot {
    // The assets directory.
    Assets,
    // The code directory, for projects with code.
    Code,
};

// What kind of file a plain file is, from its name.
enum class FileKind {
    Source,
    Header,
    BuildScript,
    Other,
};

FileKind GetFileKind(std::string_view name);
// "C++ Source", "C++ Header", "CMake Script", or for other files their extension, as in "TXT File", or "File".
std::string GetFileTypeName(std::string_view name);

struct BrowserItem {
    BrowserRoot root = BrowserRoot::Assets;
    // The folder's or the source file's; sub-assets share their file's.
    std::string path = {};
    // A folder's or file's name, such as "crate.png", or a sub-asset's, such as "Roof".
    std::string name = {};
    bool folder = false;
    // Null for folders and code.
    AssetId id = {};
    AssetType type = AssetType::Texture;
    // Made from part of a file, such as a model's mesh.
    bool subAsset = false;
    // A file whose parts are sub-assets, which can be shown under it.
    bool hasSubAssets = false;
    // A folder with folders or assets in it.
    bool hasContents = false;

    // Tells items apart: a folder by its path, an asset by its id, code by its path (see GetFolderKey()).
    std::string GetKey() const;
    // The key of the folder: "folder:textures" in the assets, "code:player" in the code.
    static std::string GetFolderKey(BrowserRoot root, std::string_view path);
    // The name shown: a file's without its extension, such as "crate" for "crate.png", since its type is shown.
    std::string GetDisplayName() const;
};

// Every folder below the assets directory, except hidden ones, whose names start with '.', in tree order.
std::vector<std::string> ListFolders(const std::filesystem::path& assetsDirectory);
// The folders right in a folder, from a list of ListFolders().
std::vector<std::string> GetSubfolders(const std::vector<std::string>& folders, std::string_view folder);
// What is right in a folder: its folders, then the assets of its files, each by name, ignoring case.
std::vector<BrowserItem> ListFolder(const AssetDatabase& database, const std::vector<std::string>& folders,
                                    std::string_view folder);
// The sub-assets of a file's asset, by name.
std::vector<BrowserItem> ListSubAssets(const AssetDatabase& database, AssetId file);
// The folders and assets anywhere whose names contain the text, ignoring case, folders first.
std::vector<BrowserItem> SearchAssets(const AssetDatabase& database, const std::vector<std::string>& folders,
                                      std::string_view text);
// What is right in a folder of the code: its folders, then its files, each by name, ignoring case. Hidden ones, whose
// names start with '.', are left out.
std::vector<BrowserItem> ListCodeFolder(const std::filesystem::path& codeDirectory,
                                        const std::vector<std::string>& folders, std::string_view folder);
// The folders and files anywhere in the code whose names contain the text, ignoring case, folders first.
std::vector<BrowserItem> SearchCode(const std::filesystem::path& codeDirectory, const std::vector<std::string>& folders,
                                    std::string_view text);

// "textures/ui" for "textures/ui/button.png", and "" for "button.png".
std::string GetParentFolder(std::string_view path);
// "button.png" for "textures/ui/button.png".
std::string GetFileName(std::string_view path);
// The folder and the name joined into a path.
std::string JoinPath(std::string_view folder, std::string_view name);
// Whether the path is the folder or anywhere inside it.
bool IsInFolder(std::string_view path, std::string_view folder);

// Why the name cannot be a file or folder name on every system; nothing when it can.
std::optional<std::string> ValidateFileName(std::string_view name);

} // namespace Spot::Editor
