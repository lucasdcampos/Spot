#pragma once

// Engine-internal: the data each type of imported asset stores after the file header. Importers encode it and the
// asset manager decodes it; a change to any of these bumps the type's importer version, so assets get imported
// again.

#include "animation/animation_data.h"
#include "assets/asset_file.h"
#include "assets/material_source.h"
#include "core/asset_id.h"
#include "graphics/image.h"
#include "graphics/mesh_data.h"
#include "graphics/texture.h"
#include "math/color.h"
#include "math/matrix.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Spot::Detail {

struct TextureFile {
    // The pixels; a placeholder when the texture is block-compressed.
    Image image;
    bool mipmaps = true;
    // Colors, read as linear light, or data read as it is.
    bool srgb = true;
    // Block-compressed (BC1 or BC3, sRGB or not), the texture is its levels instead, the first width by height, each
    // half the one before down to 1x1, made when it was imported.
    std::optional<TextureFormat> compressedFormat;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::vector<std::uint8_t>> levels;
};

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

void Encode(BinaryWriter& writer, const Skeleton& skeleton);
void Encode(BinaryWriter& writer, const AnimationClip& clip);
std::optional<Skeleton> DecodeSkeleton(std::span<const std::byte> data);
std::optional<AnimationClip> DecodeAnimationClip(std::span<const std::byte> data);

void Encode(BinaryWriter& writer, const TextureFile& texture);
void Encode(BinaryWriter& writer, const MeshData& mesh);
void Encode(BinaryWriter& writer, const MaterialSource& material);
void Encode(BinaryWriter& writer, const std::vector<ModelFileNode>& nodes);
void Encode(BinaryWriter& writer, const SoundFile& sound);
void Encode(BinaryWriter& writer, const FontFile& font);
// Scenes and prefabs are stored as the text Scene::SaveToString() makes.
void EncodeScene(BinaryWriter& writer, std::string_view text);

// Each returns nothing when the data is truncated, has extra bytes or is inconsistent.
std::optional<TextureFile> DecodeTexture(std::span<const std::byte> data);
// RGBA pixels from a level of BC1 or BC3 blocks, for GPUs that cannot sample them.
std::vector<std::uint8_t> DecompressBlocks(TextureFormat format, std::uint32_t width, std::uint32_t height,
                                           std::span<const std::uint8_t> blocks);
std::optional<MeshData> DecodeMesh(std::span<const std::byte> data);
std::optional<MaterialSource> DecodeMaterial(std::span<const std::byte> data);
std::optional<std::vector<ModelFileNode>> DecodeModel(std::span<const std::byte> data);
std::optional<SoundFile> DecodeSound(std::span<const std::byte> data);
std::optional<FontFile> DecodeFont(std::span<const std::byte> data);
std::optional<std::string> DecodeScene(std::span<const std::byte> data);

} // namespace Spot::Detail
