#include "spot/scene/net/network_transform.h"

#include "spot/core/io/archive.h"
#include "spot/net/net_quantize.h"
#include "spot/scene/entity.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace Spot {

namespace {

// Samples older than this before the shown time are no longer needed.
constexpr double SampleHistory = 1.0;
constexpr int RotationBits = 15;

} // namespace

void NetworkTransform::Serialize(Archive& archive) {
    archive.Field("precision", precision,
                  {.min = 0.0001f, .max = 0.1f, .tooltip = "How exactly positions are sent, in meters"});
    archive.Field("range", range, {.min = 1.0f, .max = 100000.0f, .tooltip = "How far from the origin it can be"});
    archive.Field(
        "teleportDistance", teleportDistance,
        {.min = 0.0f, .max = 1000.0f, .tooltip = "Farther than this in one step, it jumps instead of moving"});
    archive.Field("maxExtrapolation", maxExtrapolation,
                  {.min = 0.0f, .max = 2.0f, .tooltip = "Seconds it keeps moving past the last state it received"});
}

int NetworkTransform::GetPositionBits() const {
    const double steps = 2.0 * std::max(range, 1.0f) / std::max(precision, 1e-5f);
    const auto bits = static_cast<int>(std::bit_width(static_cast<std::uint64_t>(std::min(steps, 4.0e9))));
    return std::clamp(bits, 8, 32);
}

void NetworkTransform::Write(BitWriter& writer) const {
    const Transform& transform = GetEntity().GetTransform();
    NetQuantize::WriteVector3(writer, transform.GetWorldPosition(), -range, range, GetPositionBits());
    NetQuantize::WriteRotation(writer, transform.GetWorldRotation(), RotationBits);
}

void NetworkTransform::Read(BitReader& reader, double time) {
    const Vector3 position = NetQuantize::ReadVector3(reader, -range, range, GetPositionBits());
    const Quaternion rotation = NetQuantize::ReadRotation(reader, RotationBits);
    if (!reader.IsValid()) {
        return;
    }
    // A state older than the newest is late, and only spoils the movement.
    if (!m_samples.empty() && time <= m_samples.back().time) {
        return;
    }
    m_samples.push_back({time, position, rotation});
}

void NetworkTransform::ShowAt(double time) {
    if (m_samples.empty()) {
        return;
    }
    while (m_samples.size() > 2 && m_samples[1].time < time - SampleHistory) {
        m_samples.pop_front();
    }
    while (m_samples.size() > 2 && m_samples[1].time <= time) {
        m_samples.pop_front();
    }

    Vector3 position = m_samples.front().position;
    Quaternion rotation = m_samples.front().rotation;
    if (m_samples.size() >= 2) {
        const Sample& from = m_samples[0];
        const Sample& to = m_samples[1];
        const double span = std::max(to.time - from.time, 1e-6);
        const bool teleported = Distance(from.position, to.position) > teleportDistance;
        if (time >= to.time) {
            // Late: carry on as it was going, for a while.
            const double ahead = std::min(time - to.time, static_cast<double>(std::max(maxExtrapolation, 0.0f)));
            const Vector3 velocity =
                teleported ? Vector3{} : (to.position - from.position) * static_cast<float>(1.0 / span);
            position = to.position + velocity * static_cast<float>(ahead);
            rotation = to.rotation;
        } else if (time > from.time) {
            const auto t = static_cast<float>((time - from.time) / span);
            position = teleported ? from.position : Lerp(from.position, to.position, t);
            rotation = teleported ? from.rotation : Slerp(from.rotation, to.rotation, t);
        }
    }
    Transform& transform = GetEntity().GetTransform();
    transform.SetWorldPosition(position);
    transform.SetWorldRotation(rotation);
}

} // namespace Spot
