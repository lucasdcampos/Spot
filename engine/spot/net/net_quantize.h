#pragma once

#include "spot/core/math/quaternion.h"
#include "spot/core/math/vector.h"
#include "spot/net/bit_stream.h"

#include <cstdint>

namespace Spot::NetQuantize {

// A number in a range as an integer of that many bits (1 to 32), so its error is at most half the range over
// 2^bits - 1. Values outside the range are clamped to it.
std::uint32_t ToBits(float value, float min, float max, int bits);
float FromBits(std::uint32_t quantized, float min, float max, int bits);

void WriteFloat(BitWriter& writer, float value, float min, float max, int bits);
float ReadFloat(BitReader& reader, float min, float max, int bits);

// Each component in the same range.
void WriteVector3(BitWriter& writer, Vector3 value, float min, float max, int bits);
Vector3 ReadVector3(BitReader& reader, float min, float max, int bits);

// An angle in radians, wrapped to one turn.
void WriteAngle(BitWriter& writer, float radians, int bits);
float ReadAngle(BitReader& reader, int bits);

// A unit rotation by its three smallest components, the largest found from them: 2 + 3 * bits bits. With 15 bits, the
// error stays under a hundredth of a degree.
void WriteRotation(BitWriter& writer, Quaternion rotation, int bits = 15);
Quaternion ReadRotation(BitReader& reader, int bits = 15);

} // namespace Spot::NetQuantize
