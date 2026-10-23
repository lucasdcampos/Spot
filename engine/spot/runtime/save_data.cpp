#include "spot/runtime/save_data.h"

#include "spot/core/archive.h"
#include "spot/core/archive_state.h"
#include "spot/core/asset_id.h"
#include "spot/core/diagnostics.h"
#include "spot/core/paths.h"

#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>

namespace Spot {
namespace {

SaveDataResult Failure(SaveDataStatus status, std::string message, std::uint32_t version = 0) {
    return {status, version, std::move(message)};
}

struct TemporaryFile {
    std::filesystem::path path;
    ~TemporaryFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

SaveDataResult Commit(const std::filesystem::path& temporary, const std::filesystem::path& target,
                      const std::filesystem::path& backup, std::uint32_t version) {
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    if (!error) {
        return {SaveDataStatus::Success, version, {}};
    }
    // Windows' standard rename cannot replace an existing file. Keep it recoverable until the new file is in place.
    if (!std::filesystem::is_regular_file(target, error) || error) {
        return Failure(SaveDataStatus::IoError, "Cannot replace the save file", version);
    }
    std::filesystem::rename(target, backup, error);
    if (error) {
        return Failure(SaveDataStatus::IoError, error.message(), version);
    }
    std::filesystem::rename(temporary, target, error);
    if (error) {
        auto message = error.message();
        std::filesystem::rename(backup, target, error);
        if (error) {
            message += "; the previous save remains at " + PathToUtf8(backup);
        }
        return Failure(SaveDataStatus::IoError, message, version);
    }
    std::filesystem::remove(backup, error);
    return {SaveDataStatus::Success, version, {}};
}

} // namespace

bool SaveData::IsValidName(std::string_view fileName) {
    return !fileName.empty() && fileName != "." && fileName != ".." &&
           fileName.find_first_of("/\\:") == std::string_view::npos && fileName.find('\0') == std::string_view::npos;
}

SaveDataResult SaveData::Save(std::string_view fileName, std::uint32_t version, const Serializer& serialize) const {
    if (!IsValidName(fileName)) {
        return Failure(SaveDataStatus::InvalidName, "Save names must be plain file names");
    }
    if (!serialize || m_directory.empty()) {
        return Failure(SaveDataStatus::SerializationError, "A save directory and serializer are required");
    }
    nlohmann::json document = {{"format", "spot-save"}, {"version", version}, {"data", nlohmann::json::object()}};
    std::string text;
    try {
        Detail::ArchiveState state;
        state.nodes.push_back(&document["data"]);
        Archive archive(state);
        serialize(archive, version);
        text = document.dump(2);
    } catch (...) {
        return Failure(SaveDataStatus::SerializationError, Detail::DescribeException(std::current_exception()),
                       version);
    }
    try {
        std::error_code error;
        std::filesystem::create_directories(m_directory, error);
        if (error) {
            return Failure(SaveDataStatus::IoError, error.message(), version);
        }
        const auto target = m_directory / Utf8ToPath(fileName);
        const auto suffix = AssetId::Generate().ToString();
        const TemporaryFile temporary{m_directory / Utf8ToPath(std::string(fileName) + "." + suffix + ".tmp")};
        {
            std::ofstream stream(temporary.path, std::ios::binary | std::ios::trunc);
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            stream.close();
            if (!stream) {
                return Failure(SaveDataStatus::IoError, "Cannot write the save file", version);
            }
        }
        return Commit(temporary.path, target,
                      m_directory / Utf8ToPath(std::string(fileName) + "." + suffix + ".backup"), version);
    } catch (...) {
        return Failure(SaveDataStatus::IoError, Detail::DescribeException(std::current_exception()), version);
    }
}

SaveDataResult SaveData::Load(std::string_view fileName, std::uint32_t supportedVersion,
                              const Serializer& serialize) const {
    if (!IsValidName(fileName)) {
        return Failure(SaveDataStatus::InvalidName, "Save names must be plain file names");
    }
    if (!serialize || m_directory.empty()) {
        return Failure(SaveDataStatus::SerializationError, "A save directory and serializer are required");
    }
    nlohmann::json document;
    try {
        const auto target = m_directory / Utf8ToPath(fileName);
        std::error_code error;
        if (!std::filesystem::exists(target, error)) {
            return Failure(error ? SaveDataStatus::IoError : SaveDataStatus::NotFound,
                           error ? error.message() : "The save file does not exist");
        }
        std::ifstream stream(target, std::ios::binary);
        if (!stream) {
            return Failure(SaveDataStatus::IoError, "Cannot open the save file");
        }
        const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        if (stream.bad()) {
            return Failure(SaveDataStatus::IoError, "Cannot read the save file");
        }
        document = nlohmann::json::parse(text, nullptr, false);
    } catch (...) {
        return Failure(SaveDataStatus::IoError, Detail::DescribeException(std::current_exception()));
    }
    if (!document.is_object() || document.value("format", nlohmann::json()) != "spot-save" ||
        !document.contains("version") || !document["version"].is_number_unsigned() ||
        document["version"].get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max() ||
        !document.contains("data") || !document["data"].is_object()) {
        return Failure(SaveDataStatus::InvalidFile, "The file is not a versioned Spot save");
    }
    const auto version = document["version"].get<std::uint32_t>();
    if (version > supportedVersion) {
        return Failure(SaveDataStatus::UnsupportedVersion, "The save was written by a newer version", version);
    }
    try {
        Detail::ArchiveState state;
        state.loading = true;
        state.nodes.push_back(&document["data"]);
        Archive archive(state);
        serialize(archive, version);
        return {SaveDataStatus::Success, version, {}};
    } catch (...) {
        return Failure(SaveDataStatus::SerializationError, Detail::DescribeException(std::current_exception()),
                       version);
    }
}

} // namespace Spot
