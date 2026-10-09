#include "core/game_settings.h"

#include "assets/asset_file.h"
#include "core/log.h"
#include "core/paths.h"
#include "scene/archive.h"
#include "scene/archive_state.h"
#include "version.h"

#include <fstream>

namespace Spot {

using nlohmann::json;

namespace {

constexpr std::string_view Format = "spot-game";
constexpr int FormatVersion = 1;

} // namespace

void GameSettings::Serialize(Archive& archive) {
    archive.Field("name", name);
    archive.Field("version", version);
    archive.Field("startupScene", startupScene);
    archive.Field("importDirectory", importDirectory);
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
