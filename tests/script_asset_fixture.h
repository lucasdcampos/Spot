#pragma once

#include "temp_directory.h"

#include <spot.h>
#include <spot_import.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

struct ScriptAssetFixture {
    TempDirectory temp;
    std::filesystem::path sources = temp.GetPath() / "sources";
    std::filesystem::path imported = temp.GetPath() / "imported";
    Spot::AssetDatabase database{sources, imported};

    void Write(std::string_view path, std::string_view text) const {
        const auto filePath = sources / Spot::Utf8ToPath(path);
        std::filesystem::create_directories(filePath.parent_path());
        std::ofstream file(filePath, std::ios::binary);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        REQUIRE(file.good());
    }

    Spot::AssetId Import(std::string_view path, std::string_view text) {
        Write(path, text);
        REQUIRE(database.Refresh() == 1);
        const auto id = database.FindByPath(path);
        REQUIRE(id);
        return *id;
    }
};
