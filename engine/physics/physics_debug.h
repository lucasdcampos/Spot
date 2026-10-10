#pragma once

#include <cstdint>

namespace Spot {

class DebugLines;
class Scene;

// What a scene's PhysicsWorld draws with DebugDraw each frame; see PhysicsWorld::SetDebugDraw().
enum class PhysicsDebugFlags : std::uint32_t {
    None = 0,
    // The colliders, character controllers and joints.
    Colliders = 1 << 0,
    // Where bodies touch, with the normals pushing them apart.
    Contacts = 1 << 1,
    // The rays, casts and overlaps asked of the physics, and what they hit.
    Queries = 1 << 2,
    All = Colliders | Contacts | Queries,
};

constexpr PhysicsDebugFlags operator|(PhysicsDebugFlags a, PhysicsDebugFlags b) {
    return static_cast<PhysicsDebugFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr PhysicsDebugFlags operator&(PhysicsDebugFlags a, PhysicsDebugFlags b) {
    return static_cast<PhysicsDebugFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

constexpr bool HasFlag(PhysicsDebugFlags flags, PhysicsDebugFlags flag) {
    return (flags & flag) != PhysicsDebugFlags::None;
}

// Adds the shapes of the scene's active colliders, character controllers and joints to the lines, from their
// components alone, so it works in scenes whose physics never ran, as the editor's views show them. Static bodies
// are green, dynamic ones orange, or blue while asleep, kinematic ones purple, triggers yellow, characters cyan and
// joints white.
void DrawColliders(Scene& scene, DebugLines& lines);

} // namespace Spot
