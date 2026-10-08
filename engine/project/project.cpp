#include "project/project.h"

#include "assets/asset_file.h"
#include "core/game_module.h"
#include "core/log.h"
#include "core/paths.h"
#include "scene/archive.h"
#include "scene/archive_state.h"
#include "version.h"

#include <fstream>
#include <system_error>
#include <utility>

namespace Spot {

using nlohmann::json;

namespace {

constexpr std::string_view Format = "spot-project";
constexpr int FormatVersion = 1;

} // namespace

void ProjectSettings::Serialize(Archive& archive) {
    archive.Field("name", name);
    archive.Field("version", version);
    archive.Field("startupScene", startupScene);
    archive.Field("assetsDirectory", assetsDirectory);
}

Project::Project(std::filesystem::path file, ProjectSettings settings)
    : m_file(std::move(file)), m_settings(std::move(settings)) {
}

std::optional<ProjectSettings> Project::LoadSettingsFromString(std::string_view text) {
    json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object()) {
        Log::GetEngineLogger().Error("The project file is not valid JSON");
        return std::nullopt;
    }
    const auto format = document.find("format");
    if (format == document.end() || !format->is_string() || format->get<std::string>() != Format) {
        Log::GetEngineLogger().Error("The file is not a Spot project");
        return std::nullopt;
    }
    const auto version = document.find("formatVersion");
    if (version == document.end() || !version->is_number_integer() || version->get<int>() > FormatVersion) {
        Log::GetEngineLogger().Error("The project was saved by a newer version of Spot");
        return std::nullopt;
    }

    Detail::ArchiveState state{.loading = true, .nodes = {&document}};
    Archive archive(state);
    ProjectSettings settings;
    settings.Serialize(archive);
    return settings;
}

std::string Project::SaveSettingsToString(const ProjectSettings& settings) {
    json document = {
        {"format", Format}, {"formatVersion", FormatVersion}, {"engineVersion", std::string(GetVersionString())}};
    Detail::ArchiveState state{.loading = false, .nodes = {&document}};
    Archive archive(state);
    ProjectSettings copy = settings;
    copy.Serialize(archive);
    return document.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

std::optional<Project> Project::Load(const std::filesystem::path& file) {
    const auto bytes = Detail::ReadFileBytes(file);
    if (!bytes) {
        Log::GetEngineLogger().Error("Could not open project '{}'", PathToUtf8(file));
        return std::nullopt;
    }
    auto settings = LoadSettingsFromString({reinterpret_cast<const char*>(bytes->data()), bytes->size()});
    if (!settings) {
        Log::GetEngineLogger().Error("Could not read project '{}'", PathToUtf8(file));
        return std::nullopt;
    }
    return Project(std::filesystem::absolute(file), std::move(*settings));
}

std::optional<Project> Project::Create(const std::filesystem::path& directory, ProjectSettings settings) {
    if (settings.name.empty()) {
        Log::GetEngineLogger().Error("Projects need a name");
        return std::nullopt;
    }
    auto file = std::filesystem::absolute(directory) / Utf8ToPath(settings.name + std::string(FileExtension));
    Project project(std::move(file), std::move(settings));
    std::error_code error;
    std::filesystem::create_directories(project.GetAssetsDirectory(), error);
    if (error) {
        Log::GetEngineLogger().Error("Could not create the project folder '{}': {}", PathToUtf8(directory),
                                     error.message());
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
        Log::GetEngineLogger().Error("Could not write project '{}'", PathToUtf8(m_file));
        return false;
    }
    return true;
}

std::filesystem::path Project::GetAssetsDirectory() const {
    return GetDirectory() / Utf8ToPath(m_settings.assetsDirectory);
}

std::filesystem::path Project::GetCacheDirectory() const {
    return GetDirectory() / ".spot";
}

std::filesystem::path Project::GetImportDirectory() const {
    return GetCacheDirectory() / "imported";
}

std::filesystem::path Project::GetModuleFile() const {
    return GetCacheDirectory() / "build" / "module" / ("game" + std::string(GameModule::FileExtension));
}

} // namespace Spot
