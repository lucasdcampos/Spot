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
    WriteArray(writer, texture.image.GetPixels());
}

std::optional<TextureFile> DecodeTexture(std::span<const std::byte> data) {
    BinaryReader reader(data);
    const auto width = reader.Read<std::uint32_t>();
    const auto height = reader.Read<std::uint32_t>();
    const bool mipmaps = reader.Read<std::uint8_t>() != 0;
    auto pixels = ReadArray<std::uint8_t>(reader);
    if (!pixels || pixels->size() != static_cast<std::size_t>(width) * height * 4) {
        return std::nullopt;
    }
    return Finish(reader, TextureFile{Image(width, height, std::move(*pixels)), mipmaps});
}

void Encode(BinaryWriter& writer, const MeshData& mesh) {
    WriteArray(writer, std::span<const MeshVertex>(mesh.vertices));
    WriteArray(writer, std::span<const std::uint32_t>(mesh.indices));
}

std::optional<MeshData> DecodeMesh(std::span<const std::byte> data) {
    BinaryReader reader(data);
    auto vertices = ReadArray<MeshVertex>(reader);
    auto indices = vertices ? ReadArray<std::uint32_t>(reader) : std::nullopt;
    if (!indices) {
        return std::nullopt;
    }
    for (const std::uint32_t index : *indices) {
        if (index >= vertices->size()) {
            return std::nullopt;
        }
    }
    return Finish(reader, MeshData{std::move(*vertices), std::move(*indices)});
}

void Encode(BinaryWriter& writer, const MaterialSource& material) {
    writer.Write(material.baseColor);
    writer.Write(static_cast<std::uint8_t>(material.doubleSided));
    writer.Write(material.baseColorTexture);
    writer.Write(material.worldTextureSize);
    writer.Write(material.specular);
    writer.Write(material.shininess);
}

std::optional<MaterialSource> DecodeMaterial(std::span<const std::byte> data) {
    BinaryReader reader(data);
    MaterialSource material;
    material.baseColor = reader.Read<Color>();
    material.doubleSided = reader.Read<std::uint8_t>() != 0;
    material.baseColorTexture = reader.Read<AssetId>();
    material.worldTextureSize = reader.Read<float>();
    material.specular = reader.Read<float>();
    material.shininess = reader.Read<float>();
    return Finish(reader, material);
}

void Encode(BinaryWriter& writer, const std::vector<ModelFilePart>& parts) {
    writer.Write(static_cast<std::uint32_t>(parts.size()));
    for (const auto& part : parts) {
        writer.WriteString(part.name);
        writer.Write(part.mesh);
        writer.Write(part.material);
    }
}

std::optional<std::vector<ModelFilePart>> DecodeModel(std::span<const std::byte> data) {
    BinaryReader reader(data);
    const auto count = reader.Read<std::uint32_t>();
    std::vector<ModelFilePart> parts;
    for (std::uint32_t i = 0; i < count && reader.IsValid(); ++i) {
        ModelFilePart part;
        part.name = reader.ReadString();
        part.mesh = reader.Read<AssetId>();
        part.material = reader.Read<AssetId>();
        parts.push_back(std::move(part));
    }
    return Finish(reader, std::move(parts));
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
