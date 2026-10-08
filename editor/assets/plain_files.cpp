#include "assets/plain_files.h"

#include "core/editor_log.h"

#include <spot.h>

#include <algorithm>
#include <system_error>

namespace Spot::Editor {

std::optional<std::filesystem::path> PlainFiles::Resolve(std::string_view path) const {
    const std::filesystem::path relative = Utf8ToPath(path).lexically_normal();
    const auto first = relative.begin();
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        (first != relative.end() && (*first == ".." || *first == "."))) {
        GetEditorLogger().Error("'{}' is not a path inside '{}'", path, PathToUtf8(m_directory));
        return std::nullopt;
    }
    return m_directory / relative;
}

bool PlainFiles::CreateFolder(std::string_view path) {
    const auto folder = Resolve(path);
    if (!folder) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(*folder, error);
    if (error || !std::filesystem::is_directory(*folder, error)) {
        GetEditorLogger().Error("Could not create the folder '{}': {}", path, error.message());
        return false;
    }
    return true;
}

bool PlainFiles::Move(std::string_view from, std::string_view to) {
    const auto source = Resolve(from);
    const auto target = Resolve(to);
    if (!source || !target) {
        return false;
    }
    std::error_code error;
    if (!std::filesystem::exists(*source, error)) {
        GetEditorLogger().Error("Cannot move '{}': it does not exist", from);
        return false;
    }
    // Changing only the case of a name finds the source itself on file systems that ignore case.
    if (std::filesystem::exists(*target, error) && !std::filesystem::equivalent(*source, *target, error)) {
        GetEditorLogger().Error("Cannot move '{}' to '{}': something is there already", from, to);
        return false;
    }
    const auto [end, rest] = std::ranges::mismatch(*source, *target);
    if (end == source->end() && rest != target->end()) {
        GetEditorLogger().Error("Cannot move the folder '{}' into itself", from);
        return false;
    }
    std::filesystem::create_directories(target->parent_path(), error);
    std::filesystem::rename(*source, *target, error);
    if (error) {
        GetEditorLogger().Error("Could not move '{}' to '{}': {}", from, to, error.message());
        return false;
    }
    return true;
}

bool PlainFiles::Delete(std::string_view path) {
    const auto target = Resolve(path);
    if (!target) {
        return false;
    }
    std::error_code error;
    if (!std::filesystem::exists(*target, error)) {
        GetEditorLogger().Error("Cannot delete '{}': it does not exist", path);
        return false;
    }
    std::filesystem::remove_all(*target, error);
    if (error) {
        GetEditorLogger().Error("Could not delete '{}': {}", path, error.message());
        return false;
    }
    return true;
}

bool PlainFiles::Duplicate(std::string_view from, std::string_view to) {
    const auto source = Resolve(from);
    const auto target = Resolve(to);
    if (!source || !target) {
        return false;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(*source, error) || std::filesystem::exists(*target, error)) {
        GetEditorLogger().Error("Cannot duplicate '{}' to '{}': only files go to a path nothing uses", from, to);
        return false;
    }
    std::filesystem::create_directories(target->parent_path(), error);
    std::filesystem::copy_file(*source, *target, error);
    if (error) {
        GetEditorLogger().Error("Could not duplicate '{}': {}", from, error.message());
        return false;
    }
    return true;
}

std::vector<std::string> PlainFiles::CopyIn(std::span<const std::filesystem::path> files, std::string_view folder) {
    std::vector<std::string> copied;
    const auto destination = folder.empty() ? std::optional(m_directory) : Resolve(folder);
    if (!destination) {
        return copied;
    }
    std::error_code error;
    std::filesystem::create_directories(*destination, error);
    for (const auto& file : files) {
        const std::string name = PathToUtf8(file.filename());
        const std::string path = GetFreePath(folder.empty() ? name : std::string(folder) + "/" + name);
        std::filesystem::copy(file, m_directory / Utf8ToPath(path),
                              std::filesystem::copy_options::recursive | std::filesystem::copy_options::copy_symlinks,
                              error);
        if (error) {
            GetEditorLogger().Error("Could not copy '{}' into '{}': {}", PathToUtf8(file), PathToUtf8(m_directory),
                                    error.message());
            error.clear();
            continue;
        }
        copied.push_back(path);
    }
    return copied;
}

std::string PlainFiles::GetFreePath(std::string_view path) const {
    const std::filesystem::path wanted = Utf8ToPath(path);
    std::error_code error;
    if (!std::filesystem::exists(m_directory / wanted, error)) {
        return std::string(path);
    }
    const std::filesystem::path folder = wanted.parent_path();
    const std::string stem = PathToUtf8(wanted.stem());
    const std::string extension = PathToUtf8(wanted.extension());
    for (int number = 2;; ++number) {
        const std::filesystem::path candidate = folder / Utf8ToPath(stem + " " + std::to_string(number) + extension);
        if (!std::filesystem::exists(m_directory / candidate, error)) {
            return PathToUtf8(candidate.generic_u8string());
        }
    }
}

} // namespace Spot::Editor
