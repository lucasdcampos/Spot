#pragma once

// Engine-internal: the data each type of imported asset stores after the file header. Importers encode it and the
// asset manager decodes it; a change to any of these bumps the type's importer version, so assets get imported
// again.

#include "assets/asset_file.h"
#include "assets/asset_id.h"
#include "assets/material_source.h"
#include "graphics/color.h"
#include "graphics/image.h"
#include "graphics/mesh_data.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Spot::Detail {

struct TextureFile {
    Image image;
    bool mipmaps = true;
};

struct ModelFilePart {
    std::string name;
    AssetId mesh;
    AssetId material;
};

struct SoundFile {
    std::uint32_t sampleRate = 0;
    std::uint32_t channelCount = 0;
    // 16-bit samples, half the size of the floats sounds play from.
    std::vector<std::int16_t> samples;
};

struct FontFile {
    float pixelSize = 0.0f;
    std::vector<std::byte> fontData;
};

void Encode(BinaryWriter& writer, const TextureFile& texture);
void Encode(BinaryWriter& writer, const MeshData& mesh);
void Encode(BinaryWriter& writer, const MaterialSource& material);
void Encode(BinaryWriter& writer, const std::vector<ModelFilePart>& parts);
void Encode(BinaryWriter& writer, const SoundFile& sound);
void Encode(BinaryWriter& writer, const FontFile& font);

// Each returns nothing when the data is truncated, has extra bytes or is inconsistent.
std::optional<TextureFile> DecodeTexture(std::span<const std::byte> data);
std::optional<MeshData> DecodeMesh(std::span<const std::byte> data);
std::optional<MaterialSource> DecodeMaterial(std::span<const std::byte> data);
std::optional<std::vector<ModelFilePart>> DecodeModel(std::span<const std::byte> data);
std::optional<SoundFile> DecodeSound(std::span<const std::byte> data);
std::optional<FontFile> DecodeFont(std::span<const std::byte> data);

} // namespace Spot::Detail
