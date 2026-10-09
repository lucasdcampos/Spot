#include "modules/module_host.h"

#include "core/editor_log.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <system_error>
#include <vector>

namespace Spot::Editor {

namespace {

constexpr const char* BuildScript = "CMakeLists.txt";

// Files whose changes need a build; others, such as the backups some text editors leave, do not.
constexpr std::array CodeExtensions{".c",   ".cc",  ".cpp", ".cxx",  ".h",     ".hh", ".hpp",
                                    ".hxx", ".inl", ".ixx", ".cppm", ".cmake", ".txt"};

bool IsCode(const std::filesystem::path& path) {
    const std::string extension = PathToUtf8(path.extension());
    return std::ranges::find(CodeExtensions, extension) != CodeExtensions.end();
}

std::filesystem::file_time_type GetWriteTime(const std::filesystem::path& path) {
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    return error ? std::filesystem::file_time_type::min() : time;
}

// FNV-1a, enough to notice a change.
void Mix(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
}

} // namespace

ModuleHost::ModuleHost(const Project& project, Options options)
    : m_directory(project.GetCodeDirectory()), m_cacheDirectory(project.GetCacheDirectory()),
      m_options(std::move(options)), m_build(m_directory, project.GetBuildDirectory(), m_options.toolchain) {
    // Copies loaded by earlier runs can go; those another editor still has loaded cannot, and stay.
    std::error_code error;
    std::vector<std::filesystem::path> copies;
    for (const auto& entry : std::filesystem::directory_iterator(GetShadowDirectory(), error)) {
        copies.push_back(entry.path());
    }
    for (const std::filesystem::path& copy : copies) {
        std::filesystem::remove(copy, error);
    }
    m_codeStamp = StampCode();
    m_lastWatch = std::chrono::steady_clock::now();
}

ModuleHost::~ModuleHost() {
    if (m_module) {
        const std::filesystem::path copy = m_module->GetPath();
        m_module.reset();
        std::error_code error;
        std::filesystem::remove(copy, error);
    }
}

bool ModuleHost::HasCode() const {
    std::error_code error;
    return std::filesystem::is_regular_file(m_directory / BuildScript, error);
}

bool ModuleHost::LoadBuilt() {
    if (!HasCode()) {
        return false;
    }
    const std::filesystem::path built = m_build.GetModulePath();
    std::error_code error;
    // A module built for another editor, such as one of another configuration, would not work with this one, nor one
    // built before the engine changed: its components would call the engine's functions where they no longer are.
    const bool engineChanged =
        std::filesystem::exists(built, error) && GetWriteTime(m_options.toolchain.engineLibrary) > GetWriteTime(built);
    if (engineChanged) {
        GetEditorLogger().Info("The engine changed since the game's code was built; it loads once built again");
    }
    const bool loaded =
        m_build.IsConfigured() && std::filesystem::exists(built, error) && !engineChanged && SwapModule();

    // The code may have changed while the editor was closed.
    auto newest = std::filesystem::file_time_type::min();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(m_directory, error)) {
        if (entry.is_regular_file(error) && IsCode(entry.path())) {
            newest = std::max(newest, GetWriteTime(entry.path()));
        }
    }
    if (!loaded || newest > GetWriteTime(built)) {
        Build();
    }
    return loaded;
}

void ModuleHost::Build() {
    if (HasCode()) {
        m_unbuiltChanges = false;
        m_build.Start();
    }
}

void ModuleHost::Update() {
    if (const auto now = std::chrono::steady_clock::now(); now - m_lastWatch >= m_options.watchInterval) {
        m_lastWatch = now;
        if (const std::uint64_t stamp = StampCode(); stamp != m_codeStamp) {
            m_codeStamp = stamp;
            if (m_buildOnChange) {
                Build();
            } else {
                m_unbuiltChanges = HasCode();
            }
        }
    }
    if (m_build.Update() && m_build.GetState() == CodeBuildState::Succeeded) {
        // The build brought the module up to date with the engine, even when it found nothing to do, as when the
        // engine changed none of its headers, so it is not built again for it next time.
        const std::filesystem::path built = m_build.GetModulePath();
        if (GetWriteTime(m_options.toolchain.engineLibrary) > GetWriteTime(built)) {
            std::error_code error;
            std::filesystem::last_write_time(built, std::filesystem::file_time_type::clock::now(), error);
        }
        // Builds that found nothing to do leave the module as it was.
        m_newModule = !m_module || GetWriteTime(m_build.GetModulePath()) != m_loadedTime;
    }
}

bool ModuleHost::SwapModule() {
    m_newModule = false;
    const std::filesystem::path built = m_build.GetModulePath();
    std::error_code error;
    const auto time = std::filesystem::last_write_time(built, error);
    if (error) {
        GetEditorLogger().Error("The game's module, '{}', is missing", PathToUtf8(built));
        return false;
    }

    // Loaded from a copy, as some systems keep loaded files from being replaced, with a name of its own, as some
    // give back a library loaded before under the same name.
    const std::filesystem::path shadows = GetShadowDirectory();
    std::filesystem::create_directories(shadows, error);
    std::filesystem::path copy;
    for (int number = 1;; ++number) {
        copy = shadows / Utf8ToPath(std::format("game-{}{}", number, PathToUtf8(built.extension())));
        if (!std::filesystem::exists(copy, error)) {
            break;
        }
    }
    if (!std::filesystem::copy_file(built, copy, std::filesystem::copy_options::overwrite_existing, error)) {
        GetEditorLogger().Error("Could not copy the game's module to '{}': {}", PathToUtf8(copy), error.message());
        return false;
    }

    if (m_module) {
        const std::filesystem::path previous = m_module->GetPath();
        m_module.reset();
        std::filesystem::remove(previous, error);
    }
    m_loadedTime = time;
    m_module = GameModule::Load(copy);
    if (!m_module) {
        std::filesystem::remove(copy, error);
        return false;
    }
    return true;
}

std::uint64_t ModuleHost::StampCode() const {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(m_directory, error)) {
        if (entry.is_regular_file(error) && IsCode(entry.path())) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);

    std::uint64_t hash = 14695981039346656037ull;
    for (const std::filesystem::path& file : files) {
        const std::string name = PathToUtf8(file);
        Mix(hash, name.data(), name.size());
        const std::uintmax_t size = std::filesystem::file_size(file, error);
        Mix(hash, &size, sizeof(size));
        const auto time = GetWriteTime(file).time_since_epoch().count();
        Mix(hash, &time, sizeof(time));
    }
    return hash;
}

std::filesystem::path ModuleHost::GetShadowDirectory() const {
    return m_cacheDirectory / "modules";
}

} // namespace Spot::Editor
