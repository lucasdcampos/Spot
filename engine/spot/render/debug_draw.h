#pragma once

#include "spot/core/math/color.h"
#include "spot/core/math/quaternion.h"
#include "spot/core/math/vector.h"

#include <cstddef>
#include <span>
#include <vector>

namespace Spot {

struct DebugDrawOptions {
    Color color = Color::White;
    // How many seconds the shape stays; 0 draws it in this frame only.
    float duration = 0.0f;
    // Hidden behind what is drawn in front of it when set; drawn over everything otherwise.
    bool depthTest = true;
};

// A segment of a debug shape.
struct DebugLine {
    Vector3 from;
    Vector3 to;
    Color color;
    bool depthTest = true;
    // Seconds left until it goes away.
    float timeLeft = 0.0f;
};

// A list of debug shapes, made of lines as they are added, which a LineRenderer draws. DebugDraw keeps one that every
// view draws; tools keep their own for what only one view shows.
class DebugLines {
public:
    // Bounds the list, as code that never calls Update() would otherwise grow it forever.
    static constexpr std::size_t MaxLines = 1 << 18;

    void Line(Vector3 from, Vector3 to, const DebugDrawOptions& options = {});
    // A line with a head at its end.
    void Arrow(Vector3 from, Vector3 to, const DebugDrawOptions& options = {});
    // Three lines through the position along the axes, size long.
    void Cross(Vector3 position, float size, const DebugDrawOptions& options = {});
    void Box(Vector3 center, Vector3 size, const Quaternion& rotation = {}, const DebugDrawOptions& options = {});
    // Drawn as three circles, one around each axis.
    void Sphere(Vector3 center, float radius, const DebugDrawOptions& options = {});
    // The shape around the segment between the centers of its two end spheres.
    void Capsule(Vector3 start, Vector3 end, float radius, const DebugDrawOptions& options = {});
    void Circle(Vector3 center, Vector3 normal, float radius, const DebugDrawOptions& options = {});

    std::span<const DebugLine> GetLines() const { return m_lines; }
    // Ages the lines by the seconds, dropping those whose time is up.
    void Update(float deltaTime);
    void Clear();

private:
    void Arc(Vector3 center, Vector3 u, Vector3 v, float radius, float start, float sweep, int segments,
             const DebugDrawOptions& options);

    std::vector<DebugLine> m_lines;
    bool m_warnedFull = false;
};

// Shapes any code can draw for a frame or a while, to see what it does: where a ray went, what a trigger covers.
// They are drawn in world space over the scenes, by the SceneRenderer and by the editor's views, and are meant for
// development rather than for what a game shows.
//
//     Spot::DebugDraw::Line(from, hit.point, {.color = Spot::Color::Red});
//     Spot::DebugDraw::Sphere(hit.point, 0.1f, {.duration = 2.0f, .depthTest = false});
//
// The queue is the game thread's: shapes are queued from it and drawn by it.
namespace DebugDraw {

// The queue itself, for code that adds shapes to any DebugLines.
DebugLines& GetQueue();

void Line(Vector3 from, Vector3 to, const DebugDrawOptions& options = {});
void Arrow(Vector3 from, Vector3 to, const DebugDrawOptions& options = {});
void Cross(Vector3 position, float size, const DebugDrawOptions& options = {});
void Box(Vector3 center, Vector3 size, const Quaternion& rotation = {}, const DebugDrawOptions& options = {});
void Sphere(Vector3 center, float radius, const DebugDrawOptions& options = {});
void Capsule(Vector3 start, Vector3 end, float radius, const DebugDrawOptions& options = {});
void Circle(Vector3 center, Vector3 normal, float radius, const DebugDrawOptions& options = {});

// The lines to draw this frame, for renderers; LineRenderer::DrawDebugLines() draws them.
std::span<const DebugLine> GetLines();
// Ages the lines by the seconds, dropping those whose time is up. The application calls it once each frame was
// drawn, with the frame's real time.
void Update(float deltaTime);
void Clear();

} // namespace DebugDraw

} // namespace Spot
