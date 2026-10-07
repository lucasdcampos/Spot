#include "assets/asset_manifest.h"

#include "assets/asset_file.h"
#include "core/log.h"
#include "core/paths.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <format>
#include <fstream>

namespace Spot::Detail {

using nlohmann::json;

namespace {

constexpr int ManifestVersion = 1;

std::optional<AssetId> ReadId(const json& entry, const char* key) {
    const auto found = entry.find(key);
    if (found == entry.end() || !found->is_string()) {
        return std::nullopt;
    }
    return AssetId::Parse(found->get<std::string>());
}

std::string ReadString(const json& entry, const char* key) {
    const auto found = entry.find(key);
    return found != entry.end() && found->is_string() ? found->get<std::string>() : std::string();
}

} // namespace

std::filesystem::path Manifest::GetPath(const std::filesystem::path& importDirectory) {
    return importDirectory / "manifest.json";
}

bool Manifest::SaveToFile(const std::filesystem::path& path) const {
    json assets = json::array();
    for (const auto& entry : entries) {
        json item = {{"id", entry.id.ToString()}, {"type", GetAssetTypeName(entry.type)}, {"path", entry.path}};
        if (entry.parent.IsValid()) {
            item["name"] = entry.name;
            item["parent"] = entry.parent.ToString();
        } else {
            item["imported"] = entry.imported;
            item["hash"] = std::format("{:016x}", entry.hash);
            item["stamp"] = std::format("{:016x}", entry.stamp);
            item["dependencies"] = entry.dependencies;
        }
        assets.push_back(std::move(item));
    }
    const json document = {{"version", ManifestVersion}, {"assets", std::move(assets)}};

    std::ofstream file(path, std::ios::binary);
    file << document.dump(2, ' ', false, json::error_handler_t::replace) << '\n';
    if (!file) {
        Log::GetEngineLogger().Error("Could not write the asset manifest '{}'", PathToUtf8(path));
        return false;
    }
    return true;
}

std::optional<Manifest> Manifest::LoadFromFile(const std::filesystem::path& path) {
    const auto bytes = ReadFileBytes(path);
    if (!bytes) {
        return std::nullopt;
    }
    const auto* begin = reinterpret_cast<const char*>(bytes->data());
    const json document = json::parse(begin, begin + bytes->size(), nullptr, false);
    const auto assets = document.is_object() ? document.find("assets") : document.end();
    if (!document.is_object() || document.value("version", 0) != ManifestVersion || assets == document.end() ||
        !assets->is_array()) {
        Log::GetEngineLogger().Warning("The asset manifest '{}' is unreadable or from another version",
                                       PathToUtf8(path));
        return std::nullopt;
    }

    Manifest manifest;
    for (const auto& item : *assets) {
        if (!item.is_object()) {
            continue;
        }
        const auto id = ReadId(item, "id");
        const auto type = ParseAssetType(ReadString(item, "type"));
        if (!id || !type) {
            continue;
        }

        ManifestEntry entry;
        entry.id = *id;
        entry.type = *type;
        entry.path = ReadString(item, "path");
        entry.name = ReadString(item, "name");
        entry.parent = ReadId(item, "parent").value_or(AssetId{});
        entry.imported = item.value("imported", false);
        const std::string hash = ReadString(item, "hash");
        std::from_chars(hash.data(), hash.data() + hash.size(), entry.hash, 16);
        const std::string stamp = ReadString(item, "stamp");
        std::from_chars(stamp.data(), stamp.data() + stamp.size(), entry.stamp, 16);
        if (const auto dependencies = item.find("dependencies");
            dependencies != item.end() && dependencies->is_array()) {
            for (const auto& dependency : *dependencies) {
                if (dependency.is_string()) {
                    entry.dependencies.push_back(dependency.get<std::string>());
                }
            }
        }
        manifest.entries.push_back(std::move(entry));
    }
    return manifest;
}

} // namespace Spot::Detail
