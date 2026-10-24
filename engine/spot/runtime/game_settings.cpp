#include "spot/runtime/game_settings.h"

#include "spot/assets/asset_file.h"
#include "spot/core/debug/log.h"
#include "spot/core/io/archive.h"
#include "spot/core/io/archive_state.h"
#include "spot/core/io/paths.h"
#include "version.h"

#include <fstream>

namespace Spot {

using nlohmann::json;

namespace {

constexpr std::string_view Format = "spot-game";
constexpr int FormatVersion = 1;
constexpr std::string_view SectionsKey = "sections";

void LoadSections(SettingsSections& sections, const json& object) {
    sections.ForEach([&object](std::string_view name, auto& section) {
        const auto found = object.find(name);
        if (found != object.end() && found->is_object()) {
            json node = *found;
            Detail::ArchiveState state{.loading = true, .nodes = {&node}};
            Archive archive(state);
            section.Serialize(archive);
        }
    });
    for (const auto& [name, value] : object.items()) {
        bool known = false;
        sections.ForEach([&](std::string_view sectionName, auto&) { known = known || sectionName == name; });
        if (!known) {
            sections.unknown[name] = value.dump();
        }
    }
}

json SaveSections(const SettingsSections& sections) {
    json object = json::object();
    for (const auto& [name, text] : sections.unknown) {
        json value = json::parse(text, nullptr, false);
        if (!value.is_discarded()) {
            object[name] = std::move(value);
        }
    }
    SettingsSections copy = sections;
    copy.ForEach([&object](std::string_view name, auto& section) {
        json& node = object[std::string(name)] = json::object();
        Detail::ArchiveState state{.loading = false, .nodes = {&node}};
        Archive archive(state);
        section.Serialize(archive);
    });
    return object;
}

} // namespace

bool SettingsSections::LoadFromString(std::string_view text) {
    const json object = json::parse(text.begin(), text.end(), nullptr, false);
    if (!object.is_object()) {
        return false;
    }
    LoadSections(*this, object);
    return true;
}

std::string SettingsSections::SaveToString() const {
    return SaveSections(*this).dump(2, ' ', false, json::error_handler_t::replace);
}

void SettingsSections::Apply(Scene& scene) const {
    physics.Apply(scene);
}

void GameSettings::Serialize(Archive& archive) {
    archive.Field("name", name);
    archive.Field("version", version);
    archive.Field("startupScene", startupScene);
    archive.Field("importDirectory", importDirectory);
}

std::optional<GameSettings> GameSettings::LoadBesideExecutable() {
    const auto file = GetExecutableDirectory() / FileName;
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) {
        return std::nullopt;
    }
    return LoadFromFile(file);
}

std::optional<GameSettings> GameSettings::LoadFromString(std::string_view text) {
    json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object()) {
        Log::GetEngineLogger().Error("The game settings are not valid JSON");
        return std::nullopt;
    }
    const auto format = document.find("format");
    if (format == document.end() || !format->is_string() || format->get<std::string>() != Format) {
        Log::GetEngineLogger().Error("The file holds no game settings");
        return std::nullopt;
    }
    const auto version = document.find("formatVersion");
    if (version == document.end() || !version->is_number_integer() || version->get<int>() > FormatVersion) {
        Log::GetEngineLogger().Error("The game settings were written by a newer version of Spot");
        return std::nullopt;
    }

    Detail::ArchiveState state{.loading = true, .nodes = {&document}};
    Archive archive(state);
    GameSettings settings;
    settings.Serialize(archive);
    if (const auto sections = document.find(SectionsKey); sections != document.end() && sections->is_object()) {
        LoadSections(settings.sections, *sections);
    }
    return settings;
}

std::optional<GameSettings> GameSettings::LoadFromFile(const std::filesystem::path& file) {
    const auto bytes = Detail::ReadFileBytes(file);
    if (!bytes) {
        Log::GetEngineLogger().Error("Could not open the game settings '{}'", PathToUtf8(file));
        return std::nullopt;
    }
    auto settings = LoadFromString({reinterpret_cast<const char*>(bytes->data()), bytes->size()});
    if (!settings) {
        Log::GetEngineLogger().Error("Could not read the game settings '{}'", PathToUtf8(file));
    }
    return settings;
}

std::string GameSettings::SaveToString() const {
    json document = {
        {"format", Format}, {"formatVersion", FormatVersion}, {"engineVersion", std::string(GetVersionString())}};
    Detail::ArchiveState state{.loading = false, .nodes = {&document}};
    Archive archive(state);
    GameSettings copy = *this;
    copy.Serialize(archive);
    document[SectionsKey] = SaveSections(sections);
    return document.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

bool GameSettings::SaveToFile(const std::filesystem::path& file) const {
    std::ofstream stream(file, std::ios::binary);
    stream << SaveToString();
    if (!stream) {
        Log::GetEngineLogger().Error("Could not write the game settings '{}'", PathToUtf8(file));
        return false;
    }
    return true;
}

} // namespace Spot
