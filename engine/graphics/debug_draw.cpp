#include "graphics/debug_draw.h"

#include "core/log.h"
#include "math/angles.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Spot {

namespace {

// Bounds the queue, as code that never calls Update() would otherwise grow it forever.
constexpr std::size_t MaxLines = 1 << 18;
constexpr int CircleSegments = 32;

struct Queue {
    std::vector<DebugLine> lines;
    bool warnedFull = false;
};

Queue& GetQueue() {
    static Queue queue;
    return queue;
}

void Add(Vector3 from, Vector3 to, const DebugDrawOptions& options) {
    Queue& queue = GetQueue();
    if (queue.lines.size() >= MaxLines) {
        if (!queue.warnedFull) {
            queue.warnedFull = true;
            Log::GetEngineLogger().Warning("DebugDraw holds {} lines, so it drops more: is DebugDraw::Update() called?",
                                           MaxLines);
        }
        return;
    }
    queue.lines.push_back({.from = from,
                           .to = to,
                           .color = options.color,
                           .depthTest = options.depthTest,
                           .timeLeft = std::max(options.duration, 0.0f)});
}

// Two unit vectors perpendicular to the direction and to each other.
void GetPerpendiculars(Vector3 direction, Vector3& u, Vector3& v) {
    const Vector3 n = Normalize(direction);
    const Vector3 other = std::abs(n.y) < 0.99f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
    u = Normalize(Cross(n, other));
    v = Cross(n, u);
}

// Part of a circle in the plane of u and v, from the angle start, turning by sweep.
void Arc(Vector3 center, Vector3 u, Vector3 v, float radius, float start, float sweep, int segments,
         const DebugDrawOptions& options) {
    const auto point = [&](int i) {
        const float angle = start + sweep * static_cast<float>(i) / static_cast<float>(segments);
        return center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
    };
    Vector3 previous = point(0);
    for (int i = 1; i <= segments; ++i) {
        const Vector3 next = point(i);
        Add(previous, next, options);
        previous = next;
    }
}

} // namespace

namespace DebugDraw {

void Line(Vector3 from, Vector3 to, const DebugDrawOptions& options) {
    Add(from, to, options);
}

void Arrow(Vector3 from, Vector3 to, const DebugDrawOptions& options) {
    Add(from, to, options);
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
        Add(to, base + side * (head * 0.4f), options);
    }
}

void Cross(Vector3 position, float size, const DebugDrawOptions& options) {
    const float half = size * 0.5f;
    Add(position - Vector3{half, 0.0f, 0.0f}, position + Vector3{half, 0.0f, 0.0f}, options);
    Add(position - Vector3{0.0f, half, 0.0f}, position + Vector3{0.0f, half, 0.0f}, options);
    Add(position - Vector3{0.0f, 0.0f, half}, position + Vector3{0.0f, 0.0f, half}, options);
}

void Box(Vector3 center, Vector3 size, const Quaternion& rotation, const DebugDrawOptions& options) {
    const Vector3 half = size * 0.5f;
    const auto corner = [&](int i) {
        const Vector3 local{(i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z};
        return center + rotation * local;
    };
    // Each corner joins the corners that differ from it along one axis.
    for (int i = 0; i < 8; ++i) {
        for (const int axis : {1, 2, 4}) {
            if ((i & axis) == 0) {
                Add(corner(i), corner(i | axis), options);
            }
        }
    }
}

void Sphere(Vector3 center, float radius, const DebugDrawOptions& options) {
    const Vector3 x{1.0f, 0.0f, 0.0f};
    const Vector3 y{0.0f, 1.0f, 0.0f};
    const Vector3 z{0.0f, 0.0f, 1.0f};
    Arc(center, x, y, radius, 0.0f, 2.0f * Pi, CircleSegments, options);
    Arc(center, y, z, radius, 0.0f, 2.0f * Pi, CircleSegments, options);
    Arc(center, z, x, radius, 0.0f, 2.0f * Pi, CircleSegments, options);
}

void Capsule(Vector3 start, Vector3 end, float radius, const DebugDrawOptions& options) {
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
        Add(start + side * radius, end + side * radius, options);
    }
    // The halves of the end spheres that bulge out of the cylinder.
    for (const Vector3 side : {u, v}) {
        Arc(end, side, a, radius, 0.0f, Pi, CircleSegments / 2, options);
        Arc(start, side, -a, radius, 0.0f, Pi, CircleSegments / 2, options);
    }
}

void Circle(Vector3 center, Vector3 normal, float radius, const DebugDrawOptions& options) {
    Vector3 u;
    Vector3 v;
    GetPerpendiculars(normal, u, v);
    Arc(center, u, v, radius, 0.0f, 2.0f * Pi, CircleSegments, options);
}

std::span<const DebugLine> GetLines() {
    return GetQueue().lines;
}

void Update(float deltaTime) {
    Queue& queue = GetQueue();
    std::erase_if(queue.lines, [deltaTime](DebugLine& line) {
        line.timeLeft -= deltaTime;
        return line.timeLeft <= 0.0f;
    });
}

void Clear() {
    GetQueue().lines.clear();
}

} // namespace DebugDraw

} // namespace Spot
