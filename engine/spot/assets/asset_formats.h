#pragma once

// Engine-internal: the data each type of imported asset stores after the file header. Importers encode it and the
// asset manager decodes it; a change to any of these bumps the type's importer version, so assets get imported
// again.

#include "spot/assets/asset_file.h"
#include "spot/assets/material_source.h"
#include "spot/core/ids/asset_id.h"
#include "spot/core/math/color.h"
#include "spot/core/math/matrix.h"
#include "spot/graphics/mesh_data.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Spot::Detail {

// A node of a model file: its transform relative to its parent, and the mesh it shows with a material per submesh.
struct ModelFileNode {
    std::string name;
    Matrix4 transform;
    // An index into the nodes before it, or -1 for root nodes.
    std::int32_t parent = -1;
    AssetId mesh = {};
    std::vector<AssetId> materials = {};
    AssetId skeleton = {};
    std::vector<AssetId> animations = {};
    bool joint = false;
};

struct SoundFile {
    std::uint32_t sampleRate = 0;
    std::uint32_t channelCount = 0;
    // 16-bit samples, half the size of the floats sounds play from; empty when streamed.
    std::vector<std::int16_t> samples = {};
    // Streamed sounds keep their source's WAV or Ogg Vorbis data, which they decode while they play.
    bool streamed = false;
    std::vector<std::byte> encoded = {};
};

struct FontFile {
    float pixelSize = 0.0f;
    std::vector<std::byte> fontData;
};

void Encode(BinaryWriter& writer, const MeshData& mesh);
void Encode(BinaryWriter& writer, const MaterialSource& material);
void Encode(BinaryWriter& writer, const std::vector<ModelFileNode>& nodes);
void Encode(BinaryWriter& writer, const SoundFile& sound);
void Encode(BinaryWriter& writer, const FontFile& font);
// Scenes and prefabs are stored as the text Scene::SaveToString() makes.
void EncodeScene(BinaryWriter& writer, std::string_view text);

// Each returns nothing when the data is truncated, has extra bytes or is inconsistent.
std::optional<MeshData> DecodeMesh(std::span<const std::byte> data);
std::optional<MaterialSource> DecodeMaterial(std::span<const std::byte> data);
std::optional<std::vector<ModelFileNode>> DecodeModel(std::span<const std::byte> data);
std::optional<SoundFile> DecodeSound(std::span<const std::byte> data);
std::optional<FontFile> DecodeFont(std::span<const std::byte> data);
std::optional<std::string> DecodeScene(std::span<const std::byte> data);

} // namespace Spot::Detail
