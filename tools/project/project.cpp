#include "project/project.h"

#include "tools_log.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace Spot {

using nlohmann::json;

namespace {

constexpr std::string_view Format = "spot-project";
constexpr int FormatVersion = 1;

std::string GetString(const json& document, std::string_view key, std::string fallback) {
    const auto found = document.find(key);
    return found != document.end() && found->is_string() ? found->get<std::string>() : std::move(fallback);
}

} // namespace

Project::Project(std::filesystem::path file, ProjectSettings settings)
    : m_file(std::move(file)), m_settings(std::move(settings)) {
}

std::optional<ProjectSettings> Project::LoadSettingsFromString(std::string_view text) {
    const json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object()) {
        GetToolsLogger().Error("The project file is not valid JSON");
        return std::nullopt;
    }
    if (GetString(document, "format", {}) != Format) {
        GetToolsLogger().Error("The file is not a Spot project");
        return std::nullopt;
    }
    const auto version = document.find("formatVersion");
    if (version == document.end() || !version->is_number_integer() || version->get<int>() > FormatVersion) {
        GetToolsLogger().Error("The project was saved by a newer version of Spot");
        return std::nullopt;
    }

    ProjectSettings settings;
    settings.name = GetString(document, "name", settings.name);
    settings.version = GetString(document, "version", settings.version);
    settings.startupScene = AssetId::Parse(GetString(document, "startupScene", {})).value_or(AssetId{});
    if (const auto sections = document.find("sections"); sections != document.end() && sections->is_object()) {
        settings.sections.LoadFromString(sections->dump());
    }
    return settings;
}

std::string Project::SaveSettingsToString(const ProjectSettings& settings) {
    const json document = {
        {"format", Format},
        {"formatVersion", FormatVersion},
        {"engineVersion", std::string(GetVersionString())},
        {"name", settings.name},
        {"version", settings.version},
        {"startupScene", settings.startupScene.IsValid() ? json(settings.startupScene.ToString()) : json(nullptr)},
        {"sections", json::parse(settings.sections.SaveToString())}};
    return document.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

std::optional<Project> Project::Load(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        GetToolsLogger().Error("Could not open project '{}'", PathToUtf8(file));
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    auto settings = LoadSettingsFromString(text);
    if (!settings) {
        GetToolsLogger().Error("Could not read project '{}'", PathToUtf8(file));
        return std::nullopt;
    }
    return Project(std::filesystem::absolute(file), std::move(*settings));
}

std::optional<Project> Project::Create(const std::filesystem::path& directory, ProjectSettings settings) {
    if (settings.name.empty()) {
        GetToolsLogger().Error("Projects need a name");
        return std::nullopt;
    }
    auto file = std::filesystem::absolute(directory) / Utf8ToPath(settings.name + std::string(FileExtension));
    Project project(std::move(file), std::move(settings));
    std::error_code error;
    std::filesystem::create_directories(project.GetAssetsDirectory(), error);
    if (error) {
        GetToolsLogger().Error("Could not create the project folder '{}': {}", PathToUtf8(directory), error.message());
        return std::nullopt;
    }
    if (!project.Save()) {
        return std::nullopt;
    }
    return project;
}

bool Project::Save() const {
    std::ofstream file(m_file, std::ios::binary);
    file << SaveSettingsToString(m_settings);
    if (!file) {
        GetToolsLogger().Error("Could not write project '{}'", PathToUtf8(m_file));
        return false;
    }
    return true;
}

std::filesystem::path Project::GetAssetsDirectory() const {
    return GetDirectory() / "assets";
}

std::filesystem::path Project::GetCodeDirectory() const {
    return GetDirectory() / "src";
}

bool Project::HasCode() const {
    std::error_code error;
    return std::filesystem::is_regular_file(GetCodeDirectory() / "CMakeLists.txt", error);
}

std::filesystem::path Project::GetCacheDirectory() const {
    return GetDirectory() / ".spot";
}

std::filesystem::path Project::GetImportDirectory() const {
    return GetCacheDirectory() / "imported";
}

std::filesystem::path Project::GetBuildDirectory() const {
    return GetCacheDirectory() / "build";
}

std::filesystem::path Project::GetModuleFile() const {
    return GetBuildDirectory() / "module" / ("game" + std::string(GameModule::FileExtension));
}

std::filesystem::path Project::GetGameExecutable() const {
#if defined(_WIN32)
    return GetBuildDirectory() / "game" / "game.exe";
#else
    return GetBuildDirectory() / "game" / "game";
#endif
}

GameSettings Project::GetGameSettings() const {
    return {.name = m_settings.name,
            .version = m_settings.version,
            .startupScene = m_settings.startupScene,
            .sections = m_settings.sections};
}

} // namespace Spot
