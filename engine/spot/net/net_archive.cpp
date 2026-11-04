#include "spot/net/net_archive.h"

#include "spot/net/net_quantize.h"

#include <cmath>
#include <limits>

namespace Spot {

namespace {

// Assigns what was read only when the reader is still valid, so a field read past the end keeps its value.
template <typename T, typename Read>
void ReadInto(BitReader& reader, T& value, Read read) {
    T readValue = read();
    if (reader.IsValid()) {
        value = readValue;
    }
}

// What is not a number never is valid data, and would spread through whatever computes with it, as the physics.
float ReadFinite(BitReader& reader) {
    const float value = reader.ReadFloat();
    if (!std::isfinite(value)) {
        reader.Fail();
    }
    return value;
}

} // namespace

void NetArchiveWriter::Field(std::string_view, bool& value, const FieldOptions&) {
    m_writer->WriteBool(value);
}

void NetArchiveWriter::Field(std::string_view, int& value, const FieldOptions&) {
    m_writer->WriteVarInt(value);
}

void NetArchiveWriter::Field(std::string_view, std::uint32_t& value, const FieldOptions&) {
    m_writer->WriteVarUInt(value);
}

void NetArchiveWriter::Field(std::string_view, std::int64_t& value, const FieldOptions&) {
    m_writer->WriteVarInt(value);
}

void NetArchiveWriter::Field(std::string_view, std::uint64_t& value, const FieldOptions&) {
    m_writer->WriteVarUInt(value);
}

void NetArchiveWriter::Field(std::string_view, float& value, const FieldOptions& options) {
    if (options.netBits > 0 && options.angle) {
        NetQuantize::WriteAngle(*m_writer, value, options.netBits);
    } else if (options.netBits > 0 && options.min < options.max) {
        NetQuantize::WriteFloat(*m_writer, value, options.min, options.max, options.netBits);
    } else {
        m_writer->WriteFloat(value);
    }
}

void NetArchiveWriter::Field(std::string_view, double& value, const FieldOptions&) {
    m_writer->WriteDouble(value);
}

void NetArchiveWriter::Field(std::string_view, std::string& value, const FieldOptions&) {
    m_writer->WriteString(value);
}

void NetArchiveWriter::Field(std::string_view, Vector2& value, const FieldOptions&) {
    m_writer->WriteFloat(value.x);
    m_writer->WriteFloat(value.y);
}

void NetArchiveWriter::Field(std::string_view, Vector3& value, const FieldOptions&) {
    m_writer->WriteFloat(value.x);
    m_writer->WriteFloat(value.y);
    m_writer->WriteFloat(value.z);
}

void NetArchiveWriter::Field(std::string_view, Vector4& value, const FieldOptions&) {
    m_writer->WriteFloat(value.x);
    m_writer->WriteFloat(value.y);
    m_writer->WriteFloat(value.z);
    m_writer->WriteFloat(value.w);
}

void NetArchiveWriter::Field(std::string_view, Quaternion& value, const FieldOptions&) {
    m_writer->WriteFloat(value.x);
    m_writer->WriteFloat(value.y);
    m_writer->WriteFloat(value.z);
    m_writer->WriteFloat(value.w);
}

void NetArchiveWriter::Field(std::string_view, Color& value, const FieldOptions&) {
    m_writer->WriteFloat(value.r);
    m_writer->WriteFloat(value.g);
    m_writer->WriteFloat(value.b);
    m_writer->WriteFloat(value.a);
}

void NetArchiveWriter::Field(std::string_view, EntityId& value, const FieldOptions&) {
    m_writer->WriteVarUInt(m_entities ? m_entities->ToWire(value) : static_cast<std::uint64_t>(value));
}

void NetArchiveWriter::Field(std::string_view, AssetId& value, std::optional<AssetType>, const FieldOptions&) {
    m_writer->WriteUInt64(value.high);
    m_writer->WriteUInt64(value.low);
}

bool NetArchiveWriter::BeginObject(std::string_view, const FieldOptions&) {
    return true;
}

bool NetArchiveWriter::BeginArray(std::string_view, std::size_t& size, const FieldOptions&) {
    m_writer->WriteVarUInt(size);
    return true;
}

void NetArchiveReader::Field(std::string_view, bool& value, const FieldOptions&) {
    ReadInto(*m_reader, value, [&] { return m_reader->ReadBool(); });
}

void NetArchiveReader::Field(std::string_view, int& value, const FieldOptions&) {
    const std::int64_t read = m_reader->ReadVarInt();
    if (read < std::numeric_limits<int>::min() || read > std::numeric_limits<int>::max()) {
        m_reader->Fail();
    }
    if (m_reader->IsValid()) {
        value = static_cast<int>(read);
    }
}

void NetArchiveReader::Field(std::string_view, std::uint32_t& value, const FieldOptions&) {
    const std::uint64_t read = m_reader->ReadVarUInt();
    if (read > std::numeric_limits<std::uint32_t>::max()) {
        m_reader->Fail();
    }
    if (m_reader->IsValid()) {
        value = static_cast<std::uint32_t>(read);
    }
}

void NetArchiveReader::Field(std::string_view, std::int64_t& value, const FieldOptions&) {
    ReadInto(*m_reader, value, [&] { return m_reader->ReadVarInt(); });
}

void NetArchiveReader::Field(std::string_view, std::uint64_t& value, const FieldOptions&) {
    ReadInto(*m_reader, value, [&] { return m_reader->ReadVarUInt(); });
}

void NetArchiveReader::Field(std::string_view, float& value, const FieldOptions& options) {
    ReadInto(*m_reader, value, [&] {
        if (options.netBits > 0 && options.angle) {
            return NetQuantize::ReadAngle(*m_reader, options.netBits);
        }
        if (options.netBits > 0 && options.min < options.max) {
            return NetQuantize::ReadFloat(*m_reader, options.min, options.max, options.netBits);
        }
        return ReadFinite(*m_reader);
    });
}

void NetArchiveReader::Field(std::string_view, double& value, const FieldOptions&) {
    ReadInto(*m_reader, value, [&] {
        const double read = m_reader->ReadDouble();
        if (!std::isfinite(read)) {
            m_reader->Fail();
        }
        return read;
    });
}

void NetArchiveReader::Field(std::string_view, std::string& value, const FieldOptions&) {
    ReadInto(*m_reader, value, [&] { return m_reader->ReadString(); });
}

void NetArchiveReader::Field(std::string_view, Vector2& value, const FieldOptions&) {
    ReadInto(*m_reader, value, [&] {
        const float x = ReadFinite(*m_reader);
        return Vector2{x, ReadFinite(*m_reader)};
    });
}

void NetArchiveReader::Field(std::string_view, Vector3& value, const FieldOptions&) {
    ReadInto(*m_reader, value, [&] {
        const float x = ReadFinite(*m_reader);
        const float y = ReadFinite(*m_reader);
        return Vector3{x, y, ReadFinite(*m_reader)};
    });
}

void NetArchiveReader::Field(std::string_view, Vector4& value, const FieldOptions&) {
    ReadInto(*m_reader, value, [&] {
        const float x = ReadFinite(*m_reader);
        const float y = ReadFinite(*m_reader);
        const float z = ReadFinite(*m_reader);
        return Vector4{x, y, z, ReadFinite(*m_reader)};
    });
}

void NetArchiveReader::Field(std::string_view, Quaternion& value, const FieldOptions&) {
    ReadInto(*m_reader, value, [&] {
        const float x = ReadFinite(*m_reader);
        const float y = ReadFinite(*m_reader);
        const float z = ReadFinite(*m_reader);
        return Quaternion{x, y, z, ReadFinite(*m_reader)};
    });
}

void NetArchiveReader::Field(std::string_view, Color& value, const FieldOptions&) {
    ReadInto(*m_reader, value, [&] {
        const float r = ReadFinite(*m_reader);
        const float g = ReadFinite(*m_reader);
        const float b = ReadFinite(*m_reader);
        return Color{r, g, b, ReadFinite(*m_reader)};
    });
}

void NetArchiveReader::Field(std::string_view, EntityId& value, const FieldOptions&) {
    const std::uint64_t id = m_reader->ReadVarUInt();
    if (m_reader->IsValid()) {
        value = m_entities ? m_entities->FromWire(id) : static_cast<EntityId>(id);
    }
}

void NetArchiveReader::Field(std::string_view, AssetId& value, std::optional<AssetType>, const FieldOptions&) {
    ReadInto(*m_reader, value, [&] {
        const std::uint64_t high = m_reader->ReadUInt64();
        return AssetId{high, m_reader->ReadUInt64()};
    });
}

bool NetArchiveReader::BeginObject(std::string_view, const FieldOptions&) {
    return true;
}

bool NetArchiveReader::BeginArray(std::string_view, std::size_t& size, const FieldOptions&) {
    const std::uint64_t read = m_reader->ReadVarUInt();
    // Every element takes a bit at least in practice, so more elements than bits left is wrong data, not a large
    // allocation to make.
    if (read > m_reader->GetBitsLeft()) {
        m_reader->Fail();
    }
    if (!m_reader->IsValid()) {
        return false;
    }
    size = static_cast<std::size_t>(read);
    return true;
}

} // namespace Spot
