#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Spot {

// Writes values in as few bits as they need, for packets. Bits fill each byte from its lowest; BitReader reads them
// back in the same order, so a value is read with the same function, and bit count, it was written with.
class BitWriter {
public:
    // The lowest bits of the value, from 0 to 32.
    void WriteBits(std::uint32_t value, int bits);
    void WriteBool(bool value) { WriteBits(value ? 1u : 0u, 1); }
    void WriteUInt64(std::uint64_t value);
    // Small numbers in fewer bytes: seven bits of the number per byte.
    void WriteVarUInt(std::uint64_t value);
    // Small numbers of either sign in fewer bytes.
    void WriteVarInt(std::int64_t value);
    void WriteFloat(float value);
    void WriteDouble(double value);
    void WriteBytes(std::span<const std::byte> bytes);
    // Its size, then its bytes.
    void WriteString(std::string_view text);

    std::size_t GetBitCount() const { return m_bytes.size() * 8 + static_cast<std::size_t>(m_pendingBits); }
    // The bytes written, the last one padded with zeros; the writer is empty after.
    std::vector<std::byte> TakeBytes();

private:
    std::vector<std::byte> m_bytes;
    std::uint64_t m_pending = 0;
    int m_pendingBits = 0;
};

// Reads what a BitWriter wrote. Reading past the end, or a size that does not fit what is left, fails the reader: it
// returns zeros and empty values from then on, and IsValid() tells it, so a packet is checked once after reading it.
class BitReader {
public:
    explicit BitReader(std::span<const std::byte> bytes) : m_bytes(bytes) {}

    std::uint32_t ReadBits(int bits);
    bool ReadBool() { return ReadBits(1) != 0; }
    std::uint64_t ReadUInt64();
    std::uint64_t ReadVarUInt();
    std::int64_t ReadVarInt();
    float ReadFloat();
    double ReadDouble();
    // Fails when fewer bytes are left.
    std::vector<std::byte> ReadBytes(std::size_t count);
    std::string ReadString();

    bool IsValid() const { return !m_failed; }
    // Marks the data as wrong, as when a value read is out of its range.
    void Fail() { m_failed = true; }
    std::size_t GetBitsLeft() const { return m_failed ? 0 : m_bytes.size() * 8 - m_position; }

private:
    std::span<const std::byte> m_bytes;
    std::size_t m_position = 0;
    bool m_failed = false;
};

} // namespace Spot
