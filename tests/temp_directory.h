#pragma once

#include <spot.h>

#include <filesystem>
#include <format>
#include <system_error>

// A fresh folder under the system's temporary directory, removed with everything in it when destroyed.
class TempDirectory {
public:
    TempDirectory() {
        const auto id = Spot::AssetId::Generate();
        m_path = std::filesystem::temp_directory_path() / std::format("spot_tests_{}", id.ToString());
        std::filesystem::create_directories(m_path);
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    const std::filesystem::path& GetPath() const { return m_path; }

private:
    std::filesystem::path m_path;
};
