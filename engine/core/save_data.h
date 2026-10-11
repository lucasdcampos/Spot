#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace Spot {

class Archive;

enum class SaveDataStatus {
    Success,
    NotFound,
    InvalidName,
    InvalidFile,
    UnsupportedVersion,
    IoError,
    SerializationError
};

struct SaveDataResult {
    SaveDataStatus status = SaveDataStatus::Success;
    std::uint32_t version = 0;
    std::string message;
    explicit operator bool() const { return status == SaveDataStatus::Success; }
};

// Use the application's user data directory for game saves; an explicit directory also supports headless tools.
// A callback receives the file's version on load, allowing older fields to be migrated through Archive.
class SaveData {
public:
    using Serializer = std::function<void(Archive&, std::uint32_t)>;
    explicit SaveData(std::filesystem::path directory) : m_directory(std::move(directory)) {}
    const std::filesystem::path& GetDirectory() const { return m_directory; }
    SaveDataResult Save(std::string_view fileName, std::uint32_t version, const Serializer& serialize) const;
    SaveDataResult Load(std::string_view fileName, std::uint32_t supportedVersion, const Serializer& serialize) const;
    static bool IsValidName(std::string_view fileName);

private:
    std::filesystem::path m_directory;
};

} // namespace Spot
