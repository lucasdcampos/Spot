#include "spot/graphics/debug_draw.h"

#include "spot/core/angles.h"
#include "spot/core/log.h"

#include <algorithm>
#include <cmath>

namespace Spot {

namespace {

constexpr int CircleSegments = 32;

// Two unit vectors perpendicular to the direction and to each other.
void GetPerpendiculars(Vector3 direction, Vector3& u, Vector3& v) {
    const Vector3 n = Normalize(direction);
    const Vector3 other = std::abs(n.y) < 0.99f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
    u = Normalize(Cross(n, other));
    v = Cross(n, u);
}

} // namespace

void DebugLines::Line(Vector3 from, Vector3 to, const DebugDrawOptions& options) {
    if (m_lines.size() >= MaxLines) {
        if (!m_warnedFull) {
            m_warnedFull = true;
            Log::GetEngineLogger().Warning("Debug lines hold {} lines, so they drop more: is Update() called?",
                                           MaxLines);
        }
        return;
    }
    m_lines.push_back({.from = from,
                       .to = to,
                       .color = options.color,
                       .depthTest = options.depthTest,
                       .timeLeft = std::max(options.duration, 0.0f)});
}

void DebugLines::Arc(Vector3 center, Vector3 u, Vector3 v, float radius, float start, float sweep, int segments,
                     const DebugDrawOptions& options) {
    const auto point = [&](int i) {
        const float angle = start + sweep * static_cast<float>(i) / static_cast<float>(segments);
        return center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
    };
    Vector3 previous = point(0);
    for (int i = 1; i <= segments; ++i) {
        const Vector3 next = point(i);
        Line(previous, next, options);
        previous = next;
    }
}

void DebugLines::Arrow(Vector3 from, Vector3 to, const DebugDrawOptions& options) {
    Line(from, to, options);
    const Vector3 direction = to - from;
    const float length = Length(direction);
    if (length <= 0.0f) {
        return;
    }
    Vector3 u;
    Vector3 v;
    GetPerpendiculars(direction, u, v);
    const float head = length * 0.2f;
    const Vector3 base = to - direction * (head / length);
    for (const Vector3 side : {u, -u, v, -v}) {
        Line(to, base + side * (head * 0.4f), options);
    }
}

void DebugLines::Cross(Vector3 position, float size, const DebugDrawOptions& options) {
    const float half = size * 0.5f;
    Line(position - Vector3{half, 0.0f, 0.0f}, position + Vector3{half, 0.0f, 0.0f}, options);
    Line(position - Vector3{0.0f, half, 0.0f}, position + Vector3{0.0f, half, 0.0f}, options);
    Line(position - Vector3{0.0f, 0.0f, half}, position + Vector3{0.0f, 0.0f, half}, options);
}

void DebugLines::Box(Vector3 center, Vector3 size, const Quaternion& rotation, const DebugDrawOptions& options) {
    const Vector3 half = size * 0.5f;
    const auto corner = [&](int i) {
        const Vector3 local{(i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z};
        return center + rotation * local;
    };
    // Each corner joins the corners that differ from it along one axis.
    for (int i = 0; i < 8; ++i) {
        for (const int axis : {1, 2, 4}) {
            if ((i & axis) == 0) {
                Line(corner(i), corner(i | axis), options);
            }
        }
    }
}

void DebugLines::Sphere(Vector3 center, float radius, const DebugDrawOptions& options) {
    const Vector3 x{1.0f, 0.0f, 0.0f};
    const Vector3 y{0.0f, 1.0f, 0.0f};
    const Vector3 z{0.0f, 0.0f, 1.0f};
    Arc(center, x, y, radius, 0.0f, 2.0f * Pi, CircleSegments, options);
    Arc(center, y, z, radius, 0.0f, 2.0f * Pi, CircleSegments, options);
    Arc(center, z, x, radius, 0.0f, 2.0f * Pi, CircleSegments, options);
}

void DebugLines::Capsule(Vector3 start, Vector3 end, float radius, const DebugDrawOptions& options) {
    const Vector3 axis = end - start;
    if (LengthSquared(axis) <= 0.0f) {
        Sphere(start, radius, options);
        return;
    }
    const Vector3 a = Normalize(axis);
    Vector3 u;
    Vector3 v;
    GetPerpendiculars(axis, u, v);
    Arc(start, u, v, radius, 0.0f, 2.0f * Pi, CircleSegments, options);
    Arc(end, u, v, radius, 0.0f, 2.0f * Pi, CircleSegments, options);
    for (const Vector3 side : {u, -u, v, -v}) {
        Line(start + side * radius, end + side * radius, options);
    }
    // The halves of the end spheres that bulge out of the cylinder.
    for (const Vector3 side : {u, v}) {
        Arc(end, side, a, radius, 0.0f, Pi, CircleSegments / 2, options);
        Arc(start, side, -a, radius, 0.0f, Pi, CircleSegments / 2, options);
    }
}

void DebugLines::Circle(Vector3 center, Vector3 normal, float radius, const DebugDrawOptions& options) {
    Vector3 u;
    Vector3 v;
    GetPerpendiculars(normal, u, v);
    Arc(center, u, v, radius, 0.0f, 2.0f * Pi, CircleSegments, options);
}

void DebugLines::Update(float deltaTime) {
    std::erase_if(m_lines, [deltaTime](DebugLine& line) {
        line.timeLeft -= deltaTime;
        return line.timeLeft <= 0.0f;
    });
}

void DebugLines::Clear() {
    m_lines.clear();
}

namespace DebugDraw {

DebugLines& GetQueue() {
    static DebugLines queue;
    return queue;
}

void Line(Vector3 from, Vector3 to, const DebugDrawOptions& options) {
    GetQueue().Line(from, to, options);
}

void Arrow(Vector3 from, Vector3 to, const DebugDrawOptions& options) {
    GetQueue().Arrow(from, to, options);
}

void Cross(Vector3 position, float size, const DebugDrawOptions& options) {
    GetQueue().Cross(position, size, options);
}

void Box(Vector3 center, Vector3 size, const Quaternion& rotation, const DebugDrawOptions& options) {
    GetQueue().Box(center, size, rotation, options);
}

void Sphere(Vector3 center, float radius, const DebugDrawOptions& options) {
    GetQueue().Sphere(center, radius, options);
}

void Capsule(Vector3 start, Vector3 end, float radius, const DebugDrawOptions& options) {
    GetQueue().Capsule(start, end, radius, options);
}

void Circle(Vector3 center, Vector3 normal, float radius, const DebugDrawOptions& options) {
    GetQueue().Circle(center, normal, radius, options);
}

std::span<const DebugLine> GetLines() {
    return GetQueue().GetLines();
}

void Update(float deltaTime) {
    GetQueue().Update(deltaTime);
}

void Clear() {
    GetQueue().Clear();
}

} // namespace DebugDraw

} // namespace Spot
