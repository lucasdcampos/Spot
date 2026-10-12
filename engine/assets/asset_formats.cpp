#include "assets/asset_formats.h"

#include <cstring>
#include <limits>
#include <utility>

namespace Spot::Detail {

namespace {

// Arrays of plain values are stored as a 64-bit count followed by the values.
template <typename T>
void WriteArray(BinaryWriter& writer, std::span<const T> values) {
    writer.Write(static_cast<std::uint64_t>(values.size()));
    writer.WriteBytes(std::as_bytes(values));
}

template <typename T>
std::optional<std::vector<T>> ReadArray(BinaryReader& reader) {
    const auto count = reader.Read<std::uint64_t>();
    if (!reader.IsValid() || count > (std::numeric_limits<std::size_t>::max)() / sizeof(T)) {
        return std::nullopt;
    }
    const auto bytes = reader.ReadView(static_cast<std::size_t>(count) * sizeof(T));
    if (!reader.IsValid()) {
        return std::nullopt;
    }
    std::vector<T> values(static_cast<std::size_t>(count));
    if (!values.empty()) {
        std::memcpy(values.data(), bytes.data(), bytes.size());
    }
    return values;
}

// The data must be read in full: leftover bytes mean it was written in another layout.
template <typename T>
std::optional<T> Finish(const BinaryReader& reader, T value) {
    if (!reader.IsValid() || !reader.IsAtEnd()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

void Encode(BinaryWriter& writer, const TextureFile& texture) {
    writer.Write(texture.image.GetWidth());
    writer.Write(texture.image.GetHeight());
    writer.Write(static_cast<std::uint8_t>(texture.mipmaps));
    writer.Write(static_cast<std::uint8_t>(texture.srgb));
    WriteArray(writer, texture.image.GetPixels());
}

std::optional<TextureFile> DecodeTexture(std::span<const std::byte> data) {
    BinaryReader reader(data);
    const auto width = reader.Read<std::uint32_t>();
    const auto height = reader.Read<std::uint32_t>();
    const bool mipmaps = reader.Read<std::uint8_t>() != 0;
    const bool srgb = reader.Read<std::uint8_t>() != 0;
    auto pixels = ReadArray<std::uint8_t>(reader);
    if (!pixels || pixels->size() != static_cast<std::size_t>(width) * height * 4) {
        return std::nullopt;
    }
    return Finish(reader, TextureFile{Image(width, height, std::move(*pixels)), mipmaps, srgb});
}

void Encode(BinaryWriter& writer, const MeshData& mesh) {
    WriteArray(writer, std::span<const MeshVertex>(mesh.vertices));
    WriteArray(writer, std::span<const std::uint32_t>(mesh.indices));
    WriteArray(writer, std::span<const SubMesh>(mesh.submeshes));
    WriteArray(writer, std::span<const SkinVertex>(mesh.skin));
}

std::optional<MeshData> DecodeMesh(std::span<const std::byte> data) {
    BinaryReader reader(data);
    auto vertices = ReadArray<MeshVertex>(reader);
    auto indices = vertices ? ReadArray<std::uint32_t>(reader) : std::nullopt;
    auto submeshes = indices ? ReadArray<SubMesh>(reader) : std::nullopt;
    auto skin = submeshes ? ReadArray<SkinVertex>(reader) : std::nullopt;
    if (!skin || (!skin->empty() && skin->size() != vertices->size())) {
        return std::nullopt;
    }
    for (const std::uint32_t index : *indices) {
        if (index >= vertices->size()) {
            return std::nullopt;
        }
    }
    for (const SubMesh& submesh : *submeshes) {
        if (static_cast<std::uint64_t>(submesh.firstIndex) + submesh.indexCount > indices->size()) {
            return std::nullopt;
        }
    }
    return Finish(reader, MeshData{std::move(*vertices), std::move(*indices), std::move(*submeshes), std::move(*skin)});
}

void Encode(BinaryWriter& writer, const MaterialSource& material) {
    writer.Write(material.baseColor);
    writer.Write(material.baseColorTexture);
    writer.Write(material.metallic);
    writer.Write(material.roughness);
    writer.Write(material.metallicRoughnessTexture);
    writer.Write(material.normalTexture);
    writer.Write(material.normalScale);
    writer.Write(material.occlusionTexture);
    writer.Write(material.occlusionStrength);
    writer.Write(material.emissive);
    writer.Write(material.emissiveTexture);
    writer.Write(static_cast<std::uint8_t>(material.alphaMode));
    writer.Write(material.alphaCutoff);
    writer.Write(static_cast<std::uint8_t>(material.doubleSided));
    writer.Write(material.worldTextureSize);
}

std::optional<MaterialSource> DecodeMaterial(std::span<const std::byte> data) {
    BinaryReader reader(data);
    MaterialSource material;
    material.baseColor = reader.Read<Color>();
    material.baseColorTexture = reader.Read<AssetId>();
    material.metallic = reader.Read<float>();
    material.roughness = reader.Read<float>();
    material.metallicRoughnessTexture = reader.Read<AssetId>();
    material.normalTexture = reader.Read<AssetId>();
    material.normalScale = reader.Read<float>();
    material.occlusionTexture = reader.Read<AssetId>();
    material.occlusionStrength = reader.Read<float>();
    material.emissive = reader.Read<Color>();
    material.emissiveTexture = reader.Read<AssetId>();
    const auto alphaMode = reader.Read<std::uint8_t>();
    if (alphaMode > static_cast<std::uint8_t>(AlphaMode::Transparent)) {
        return std::nullopt;
    }
    material.alphaMode = static_cast<AlphaMode>(alphaMode);
    material.alphaCutoff = reader.Read<float>();
    material.doubleSided = reader.Read<std::uint8_t>() != 0;
    material.worldTextureSize = reader.Read<float>();
    return Finish(reader, material);
}

void Encode(BinaryWriter& writer, const std::vector<ModelFileNode>& nodes) {
    writer.Write(static_cast<std::uint32_t>(nodes.size()));
    for (const auto& node : nodes) {
        writer.WriteString(node.name);
        writer.Write(node.transform);
        writer.Write(node.parent);
        writer.Write(node.mesh);
        WriteArray(writer, std::span<const AssetId>(node.materials));
        writer.Write(node.skeleton);
        WriteArray(writer, std::span<const AssetId>(node.animations));
        writer.Write(static_cast<std::uint8_t>(node.joint));
    }
}

std::optional<std::vector<ModelFileNode>> DecodeModel(std::span<const std::byte> data) {
    BinaryReader reader(data);
    const auto count = reader.Read<std::uint32_t>();
    std::vector<ModelFileNode> nodes;
    for (std::uint32_t i = 0; i < count && reader.IsValid(); ++i) {
        ModelFileNode node;
        node.name = reader.ReadString();
        node.transform = reader.Read<Matrix4>();
        node.parent = reader.Read<std::int32_t>();
        node.mesh = reader.Read<AssetId>();
        auto materials = ReadArray<AssetId>(reader);
        // Parents come before their children.
        if (!materials || node.parent >= static_cast<std::int32_t>(i) || node.parent < -1) {
            return std::nullopt;
        }
        node.materials = std::move(*materials);
        node.skeleton = reader.Read<AssetId>();
        auto animations = ReadArray<AssetId>(reader);
        if (!animations) {
            return std::nullopt;
        }
        node.animations = std::move(*animations);
        node.joint = reader.Read<std::uint8_t>() != 0;
        nodes.push_back(std::move(node));
    }
    return Finish(reader, std::move(nodes));
}

void Encode(BinaryWriter& writer, const SoundFile& sound) {
    writer.Write(sound.sampleRate);
    writer.Write(sound.channelCount);
    writer.Write(static_cast<std::uint8_t>(sound.streamed));
    if (sound.streamed) {
        WriteArray(writer, std::span<const std::byte>(sound.encoded));
    } else {
        WriteArray(writer, std::span<const std::int16_t>(sound.samples));
    }
}

std::optional<SoundFile> DecodeSound(std::span<const std::byte> data) {
    BinaryReader reader(data);
    SoundFile sound;
    sound.sampleRate = reader.Read<std::uint32_t>();
    sound.channelCount = reader.Read<std::uint32_t>();
    sound.streamed = reader.Read<std::uint8_t>() != 0;
    if (sound.sampleRate == 0 || (sound.channelCount != 1 && sound.channelCount != 2)) {
        return std::nullopt;
    }
    if (sound.streamed) {
        auto encoded = ReadArray<std::byte>(reader);
        if (!encoded || encoded->empty()) {
            return std::nullopt;
        }
        sound.encoded = std::move(*encoded);
        return Finish(reader, std::move(sound));
    }
    auto samples = ReadArray<std::int16_t>(reader);
    if (!samples || samples->size() % sound.channelCount != 0) {
        return std::nullopt;
    }
    sound.samples = std::move(*samples);
    return Finish(reader, std::move(sound));
}

void Encode(BinaryWriter& writer, const FontFile& font) {
    writer.Write(font.pixelSize);
    WriteArray(writer, std::span<const std::byte>(font.fontData));
}

std::optional<FontFile> DecodeFont(std::span<const std::byte> data) {
    BinaryReader reader(data);
    FontFile font;
    font.pixelSize = reader.Read<float>();
    auto fontData = ReadArray<std::byte>(reader);
    if (!fontData || !(font.pixelSize > 0.0f)) {
        return std::nullopt;
    }
    font.fontData = std::move(*fontData);
    return Finish(reader, std::move(font));
}

void EncodeScene(BinaryWriter& writer, std::string_view text) {
    WriteArray(writer, std::span<const char>(text));
}

std::optional<std::string> DecodeScene(std::span<const std::byte> data) {
    BinaryReader reader(data);
    auto text = ReadArray<char>(reader);
    if (!text) {
        return std::nullopt;
    }
    return Finish(reader, std::string(text->begin(), text->end()));
}

} // namespace Spot::Detail
