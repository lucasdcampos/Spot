#pragma once

#include "spot/core/ids/entity_id.h"
#include "spot/core/io/archive.h"
#include "spot/net/bit_stream.h"

#include <cstdint>

namespace Spot {

// How entity references cross the network: entity ids are not the same on two machines, so whoever replicates
// entities turns them into ids both sides know. Without one, ids are sent as they are.
class NetEntityMapper {
public:
    virtual ~NetEntityMapper() = default;
    // 0 for an entity the other side does not know.
    virtual std::uint64_t ToWire(EntityId entity) const = 0;
    // EntityId::Invalid for an id this side does not know.
    virtual EntityId FromWire(std::uint64_t id) const = 0;
};

// Writes an Archive's fields into a BitWriter, without their names: whatever reads them must describe the same fields
// in the same order, as both ends do when they run the same Serialize(). Numbers are exact; values to send in fewer
// bits are written with NetQuantize by code of their own.
class NetArchiveWriter final : public ArchiveBackend {
public:
    explicit NetArchiveWriter(BitWriter& writer, const NetEntityMapper* entities = nullptr)
        : m_writer(&writer), m_entities(entities) {}

    bool IsLoading() const override { return false; }

    void Field(std::string_view name, bool& value, const FieldOptions& options) override;
    void Field(std::string_view name, int& value, const FieldOptions& options) override;
    void Field(std::string_view name, std::uint32_t& value, const FieldOptions& options) override;
    void Field(std::string_view name, std::int64_t& value, const FieldOptions& options) override;
    void Field(std::string_view name, std::uint64_t& value, const FieldOptions& options) override;
    void Field(std::string_view name, float& value, const FieldOptions& options) override;
    void Field(std::string_view name, double& value, const FieldOptions& options) override;
    void Field(std::string_view name, std::string& value, const FieldOptions& options) override;
    void Field(std::string_view name, Vector2& value, const FieldOptions& options) override;
    void Field(std::string_view name, Vector3& value, const FieldOptions& options) override;
    void Field(std::string_view name, Vector4& value, const FieldOptions& options) override;
    void Field(std::string_view name, Quaternion& value, const FieldOptions& options) override;
    void Field(std::string_view name, Color& value, const FieldOptions& options) override;
    void Field(std::string_view name, EntityId& value, const FieldOptions& options) override;
    void Field(std::string_view name, AssetId& value, std::optional<AssetType> type,
               const FieldOptions& options) override;

    bool BeginObject(std::string_view name, const FieldOptions& options) override;
    void EndObject() override {}
    bool BeginArray(std::string_view name, std::size_t& size, const FieldOptions& options) override;
    void BeginElement(std::size_t /*index*/) override {}
    void EndElement() override {}
    void EndArray() override {}

private:
    BitWriter* m_writer;
    const NetEntityMapper* m_entities;
};

// Reads what a NetArchiveWriter wrote. When the data runs out or is wrong, the reader fails (BitReader::IsValid())
// and the fields read from then on keep their values.
class NetArchiveReader final : public ArchiveBackend {
public:
    explicit NetArchiveReader(BitReader& reader, const NetEntityMapper* entities = nullptr)
        : m_reader(&reader), m_entities(entities) {}

    bool IsLoading() const override { return true; }

    void Field(std::string_view name, bool& value, const FieldOptions& options) override;
    void Field(std::string_view name, int& value, const FieldOptions& options) override;
    void Field(std::string_view name, std::uint32_t& value, const FieldOptions& options) override;
    void Field(std::string_view name, std::int64_t& value, const FieldOptions& options) override;
    void Field(std::string_view name, std::uint64_t& value, const FieldOptions& options) override;
    void Field(std::string_view name, float& value, const FieldOptions& options) override;
    void Field(std::string_view name, double& value, const FieldOptions& options) override;
    void Field(std::string_view name, std::string& value, const FieldOptions& options) override;
    void Field(std::string_view name, Vector2& value, const FieldOptions& options) override;
    void Field(std::string_view name, Vector3& value, const FieldOptions& options) override;
    void Field(std::string_view name, Vector4& value, const FieldOptions& options) override;
    void Field(std::string_view name, Quaternion& value, const FieldOptions& options) override;
    void Field(std::string_view name, Color& value, const FieldOptions& options) override;
    void Field(std::string_view name, EntityId& value, const FieldOptions& options) override;
    void Field(std::string_view name, AssetId& value, std::optional<AssetType> type,
               const FieldOptions& options) override;

    bool BeginObject(std::string_view name, const FieldOptions& options) override;
    void EndObject() override {}
    bool BeginArray(std::string_view name, std::size_t& size, const FieldOptions& options) override;
    void BeginElement(std::size_t /*index*/) override {}
    void EndElement() override {}
    void EndArray() override {}

private:
    BitReader* m_reader;
    const NetEntityMapper* m_entities;
};

// Writes what the value's Serialize() describes.
template <ArchiveSerializable T>
void WriteArchived(BitWriter& writer, T& value, const NetEntityMapper* entities = nullptr) {
    NetArchiveWriter backend(writer, entities);
    Archive archive(backend);
    value.Serialize(archive);
}

// Reads into the value what WriteArchived() wrote; false when the data was wrong.
template <ArchiveSerializable T>
bool ReadArchived(BitReader& reader, T& value, const NetEntityMapper* entities = nullptr) {
    NetArchiveReader backend(reader, entities);
    Archive archive(backend);
    value.Serialize(archive);
    return reader.IsValid();
}

} // namespace Spot
