#include "spot/core/io/file_io.h"

#include <fstream>

namespace Spot::Detail {

std::optional<std::vector<std::byte>> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::nullopt;
    }
    const std::streamsize size = file.tellg();
    file.seekg(0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file) {
        return std::nullopt;
    }
    return bytes;
}

} // namespace Spot::Detail
