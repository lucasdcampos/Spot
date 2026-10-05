#include "graphics/font.h"

#include "core/log.h"
#include "core/paths.h"
#include "graphics/graphics_device.h"
#include "graphics/text_layout.h"

#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace Spot {

std::unique_ptr<Font> Font::Load(GraphicsDevice& device, const std::filesystem::path& path, float pixelSize) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        Log::GetEngineLogger().Error("Could not open font '{}'", PathToUtf8(path));
        return nullptr;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

    auto font = LoadFromMemory(device, std::as_bytes(std::span(bytes)), pixelSize);
    if (!font) {
        Log::GetEngineLogger().Error("Could not load font '{}'", PathToUtf8(path));
    }
    return font;
}

std::unique_ptr<Font> Font::LoadFromMemory(GraphicsDevice& device, std::span<const std::byte> data, float pixelSize) {
    auto atlas = FontAtlas::Create(data, pixelSize);
    if (!atlas) {
        return nullptr;
    }
    auto texture = device.CreateTexture(atlas->GetImage());
    if (!texture) {
        return nullptr;
    }
    return std::unique_ptr<Font>(new Font(std::move(*atlas), std::move(texture)));
}

Font::Font(FontAtlas atlas, std::unique_ptr<Texture> texture)
    : m_atlas(std::move(atlas)), m_texture(std::move(texture)) {
}

Font::~Font() = default;

Vector2 Font::MeasureText(std::string_view text) const {
    return Spot::MeasureText(m_atlas, text);
}

} // namespace Spot
