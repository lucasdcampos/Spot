#include "spot/net/bit_stream.h"

#include "spot/core/debug/assert.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace Spot {

void BitWriter::WriteBits(std::uint32_t value, int bits) {
    SPOT_ASSERT(bits >= 0 && bits <= 32, "A value is written in 0 to 32 bits");
    if (bits < 32) {
        value &= (1u << bits) - 1u;
    }
    m_pending |= static_cast<std::uint64_t>(value) << m_pendingBits;
    m_pendingBits += bits;
    while (m_pendingBits >= 8) {
        m_bytes.push_back(static_cast<std::byte>(m_pending & 0xFFu));
        m_pending >>= 8;
        m_pendingBits -= 8;
    }
}

void BitWriter::WriteUInt64(std::uint64_t value) {
    WriteBits(static_cast<std::uint32_t>(value), 32);
    WriteBits(static_cast<std::uint32_t>(value >> 32), 32);
}

void BitWriter::WriteVarUInt(std::uint64_t value) {
    while (value >= 0x80u) {
        WriteBits(static_cast<std::uint32_t>(value & 0x7Fu) | 0x80u, 8);
        value >>= 7;
    }
    WriteBits(static_cast<std::uint32_t>(value), 8);
}

void BitWriter::WriteVarInt(std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    WriteVarUInt((bits << 1) ^ (value < 0 ? ~std::uint64_t{0} : 0u));
}

void BitWriter::WriteFloat(float value) {
    WriteBits(std::bit_cast<std::uint32_t>(value), 32);
}

void BitWriter::WriteDouble(double value) {
    WriteUInt64(std::bit_cast<std::uint64_t>(value));
}

void BitWriter::WriteBytes(std::span<const std::byte> bytes) {
    if (m_pendingBits == 0) {
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
        return;
    }
    for (std::byte byte : bytes) {
        WriteBits(static_cast<std::uint32_t>(byte), 8);
    }
}

void BitWriter::WriteString(std::string_view text) {
    WriteVarUInt(text.size());
    WriteBytes(std::as_bytes(std::span(text.data(), text.size())));
}

std::vector<std::byte> BitWriter::TakeBytes() {
    if (m_pendingBits > 0) {
        m_bytes.push_back(static_cast<std::byte>(m_pending & 0xFFu));
    }
    m_pending = 0;
    m_pendingBits = 0;
    return std::exchange(m_bytes, {});
}

std::uint32_t BitReader::ReadBits(int bits) {
    SPOT_ASSERT(bits >= 0 && bits <= 32, "A value is read in 0 to 32 bits");
    if (m_failed || m_position + static_cast<std::size_t>(bits) > m_bytes.size() * 8) {
        m_failed = true;
        return 0;
    }
    std::uint32_t value = 0;
    int read = 0;
    while (read < bits) {
        const int offset = static_cast<int>(m_position & 7u);
        const int take = std::min(8 - offset, bits - read);
        const std::uint32_t byte = static_cast<std::uint32_t>(m_bytes[m_position >> 3]) >> offset;
        value |= (byte & ((1u << take) - 1u)) << read;
        read += take;
        m_position += static_cast<std::size_t>(take);
    }
    return value;
}

std::uint64_t BitReader::ReadUInt64() {
    const std::uint64_t low = ReadBits(32);
    return low | (static_cast<std::uint64_t>(ReadBits(32)) << 32);
}

std::uint64_t BitReader::ReadVarUInt() {
    std::uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        const std::uint32_t byte = ReadBits(8);
        value |= static_cast<std::uint64_t>(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0) {
            return value;
        }
    }
    m_failed = true;
    return 0;
}

std::int64_t BitReader::ReadVarInt() {
    const std::uint64_t bits = ReadVarUInt();
    return static_cast<std::int64_t>((bits >> 1) ^ (~(bits & 1u) + 1u));
}

float BitReader::ReadFloat() {
    return std::bit_cast<float>(ReadBits(32));
}

double BitReader::ReadDouble() {
    return std::bit_cast<double>(ReadUInt64());
}

std::vector<std::byte> BitReader::ReadBytes(std::size_t count) {
    if (m_failed || count > GetBitsLeft() / 8) {
        m_failed = true;
        return {};
    }
    std::vector<std::byte> bytes(count);
    if ((m_position & 7u) == 0) {
        const auto start = m_bytes.begin() + static_cast<std::ptrdiff_t>(m_position >> 3);
        std::copy(start, start + static_cast<std::ptrdiff_t>(count), bytes.begin());
        m_position += count * 8;
        return bytes;
    }
    for (std::byte& byte : bytes) {
        byte = static_cast<std::byte>(ReadBits(8));
    }
    return bytes;
}

std::string BitReader::ReadString() {
    const std::uint64_t size = ReadVarUInt();
    if (m_failed || size > GetBitsLeft() / 8) {
        m_failed = true;
        return {};
    }
    const std::vector<std::byte> bytes = ReadBytes(static_cast<std::size_t>(size));
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace Spot
