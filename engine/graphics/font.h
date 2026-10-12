#pragma once

#include "graphics/font_atlas.h"
#include "math/vector.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>

namespace Spot {

class GraphicsDevice;
class Texture;

// A TrueType or OpenType font at one pixel size, with its glyphs in a GPU texture, ready for Renderer2D::DrawString().
// Signed distance glyphs keep their edges smooth when scaled; one font can serve multiple display sizes.
class Font {
public:
    // Return null, after logging why, when the file cannot be read or is not a usable font.
    static std::unique_ptr<Font> Load(GraphicsDevice& device, const std::filesystem::path& path, float pixelSize);
    static std::unique_ptr<Font> LoadFromMemory(GraphicsDevice& device, std::span<const std::byte> data,
                                                float pixelSize);

    ~Font();

    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    const FontAtlas& GetAtlas() const { return m_atlas; }
    const Texture& GetTexture() const { return *m_texture; }

    float GetPixelSize() const { return m_atlas.GetPixelSize(); }
    float GetLineHeight() const { return m_atlas.GetLineHeight(); }
    // The size of the text's box in pixels at the font's size.
    Vector2 MeasureText(std::string_view text) const;

private:
    Font(FontAtlas atlas, std::unique_ptr<Texture> texture);

    FontAtlas m_atlas;
    std::unique_ptr<Texture> m_texture;
};

} // namespace Spot
