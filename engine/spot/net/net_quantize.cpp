#include "spot/net/net_quantize.h"

#include "spot/core/debug/assert.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Spot::NetQuantize {

namespace {

std::uint32_t MaxValue(int bits) {
    return bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1u;
}

} // namespace

std::uint32_t ToBits(float value, float min, float max, int bits) {
    SPOT_ASSERT(bits >= 1 && bits <= 32 && max > min, "A quantized range needs 1 to 32 bits and max above min");
    if (!std::isfinite(value)) {
        value = min;
    }
    const double t = std::clamp((static_cast<double>(value) - min) / (static_cast<double>(max) - min), 0.0, 1.0);
    return static_cast<std::uint32_t>(std::llround(t * MaxValue(bits)));
}

float FromBits(std::uint32_t quantized, float min, float max, int bits) {
    const double t = static_cast<double>(std::min(quantized, MaxValue(bits))) / MaxValue(bits);
    return static_cast<float>(min + t * (static_cast<double>(max) - min));
}

void WriteFloat(BitWriter& writer, float value, float min, float max, int bits) {
    writer.WriteBits(ToBits(value, min, max, bits), bits);
}

float ReadFloat(BitReader& reader, float min, float max, int bits) {
    return FromBits(reader.ReadBits(bits), min, max, bits);
}

void WriteVector3(BitWriter& writer, Vector3 value, float min, float max, int bits) {
    WriteFloat(writer, value.x, min, max, bits);
    WriteFloat(writer, value.y, min, max, bits);
    WriteFloat(writer, value.z, min, max, bits);
}

Vector3 ReadVector3(BitReader& reader, float min, float max, int bits) {
    const float x = ReadFloat(reader, min, max, bits);
    const float y = ReadFloat(reader, min, max, bits);
    const float z = ReadFloat(reader, min, max, bits);
    return {x, y, z};
}

void WriteAngle(BitWriter& writer, float radians, int bits) {
    constexpr float Turn = 2.0f * std::numbers::pi_v<float>;
    float wrapped = std::isfinite(radians) ? std::fmod(radians, Turn) : 0.0f;
    if (wrapped < 0.0f) {
        wrapped += Turn;
    }
    // The last step is the same angle as 0, so a turn has 2^bits steps.
    const auto steps = static_cast<std::uint64_t>(MaxValue(bits)) + 1u;
    const auto step = static_cast<std::uint64_t>(std::llround(wrapped / Turn * static_cast<double>(steps))) % steps;
    writer.WriteBits(static_cast<std::uint32_t>(step), bits);
}

float ReadAngle(BitReader& reader, int bits) {
    constexpr double Turn = 2.0 * std::numbers::pi;
    const auto steps = static_cast<double>(static_cast<std::uint64_t>(MaxValue(bits)) + 1u);
    const double angle = reader.ReadBits(bits) / steps * Turn;
    return static_cast<float>(angle > std::numbers::pi ? angle - Turn : angle);
}

void WriteRotation(BitWriter& writer, Quaternion rotation, int bits) {
    float components[4] = {rotation.x, rotation.y, rotation.z, rotation.w};
    const float length = std::sqrt(components[0] * components[0] + components[1] * components[1] +
                                   components[2] * components[2] + components[3] * components[3]);
    if (!(length > 0.0f) || !std::isfinite(length)) {
        components[0] = components[1] = components[2] = 0.0f;
        components[3] = 1.0f;
    } else {
        for (float& component : components) {
            component /= length;
        }
    }
    int largest = 0;
    for (int i = 1; i < 4; ++i) {
        if (std::abs(components[i]) > std::abs(components[largest])) {
            largest = i;
        }
    }
    // q and -q are the same rotation: the largest is made positive, so it need not be sent.
    const float sign = components[largest] < 0.0f ? -1.0f : 1.0f;
    writer.WriteBits(static_cast<std::uint32_t>(largest), 2);
    constexpr float Limit = std::numbers::sqrt2_v<float> / 2.0f;
    for (int i = 0; i < 4; ++i) {
        if (i != largest) {
            WriteFloat(writer, components[i] * sign, -Limit, Limit, bits);
        }
    }
}

Quaternion ReadRotation(BitReader& reader, int bits) {
    const auto largest = static_cast<int>(reader.ReadBits(2));
    constexpr float Limit = std::numbers::sqrt2_v<float> / 2.0f;
    float components[4] = {};
    float sum = 0.0f;
    for (int i = 0; i < 4; ++i) {
        if (i != largest) {
            components[i] = ReadFloat(reader, -Limit, Limit, bits);
            sum += components[i] * components[i];
        }
    }
    components[largest] = std::sqrt(std::max(0.0f, 1.0f - sum));
    return {components[0], components[1], components[2], components[3]};
}

} // namespace Spot::NetQuantize
