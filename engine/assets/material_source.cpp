#include "assets/material_source.h"

#include "assets/asset_file.h"
#include "core/log.h"
#include "core/paths.h"
#include "scene/archive.h"
#include "scene/archive_state.h"

#include <fstream>

namespace Spot {

using nlohmann::json;

void MaterialSource::Serialize(Archive& archive) {
    archive.Field("baseColor", baseColor);
    archive.Field("baseColorTexture", baseColorTexture);
    archive.Field("doubleSided", doubleSided);
    archive.Field("worldTextureSize", worldTextureSize);
    archive.Field("specular", specular);
    archive.Field("shininess", shininess);
}

std::string MaterialSource::SaveToString() const {
    json document = json::object();
    Detail::ArchiveState state{.loading = false, .nodes = {&document}};
    Archive archive(state);
    MaterialSource copy = *this;
    copy.Serialize(archive);
    return document.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

bool MaterialSource::SaveToFile(const std::filesystem::path& path) const {
    std::ofstream file(path, std::ios::binary);
    file << SaveToString();
    if (!file) {
        Log::GetEngineLogger().Error("Could not write material '{}'", PathToUtf8(path));
        return false;
    }
    return true;
}

std::optional<MaterialSource> MaterialSource::LoadFromString(std::string_view text) {
    json document = json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object()) {
        Log::GetEngineLogger().Error("Material is not a JSON object");
        return std::nullopt;
    }
    Detail::ArchiveState state{.loading = true, .nodes = {&document}};
    Archive archive(state);
    MaterialSource material;
    material.Serialize(archive);
    return material;
}

std::optional<MaterialSource> MaterialSource::LoadFromFile(const std::filesystem::path& path) {
    const auto bytes = Detail::ReadFileBytes(path);
    if (!bytes) {
        Log::GetEngineLogger().Error("Could not open material '{}'", PathToUtf8(path));
        return std::nullopt;
    }
    auto material = LoadFromString({reinterpret_cast<const char*>(bytes->data()), bytes->size()});
    if (!material) {
        Log::GetEngineLogger().Error("Could not read material '{}'", PathToUtf8(path));
    }
    return material;
}

} // namespace Spot
