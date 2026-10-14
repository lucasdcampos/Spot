#pragma once

#include <spot.h>

#include <cstdint>
#include <functional>
#include <span>

// Meshes of the props, centered on the box around them, with front faces winding counterclockwise.
//
// Round shapes share a texture layout: their sides take the top half of the texture, going around it from left to
// right, and their flat ends take the bottom half, the top end a disc centered at (0.25, 0.75) and the bottom end one
// centered at (0.75, 0.75), each of radius 0.24.
namespace Generator::Shapes {

// The texture repeats every uvRepeat units on each face, starting at a corner, so faces of any size show it at the
// same scale. Long faces run the texture's X along the box's longest side.
Spot::MeshData Box(Spot::Vector3 size, float uvRepeat);
// A box whose edges and corners are cut off by the bevel, so its silhouette is softer and its edges catch the light.
// Every face is flat; the texture is projected along each face's main axis, repeating every uvRepeat units.
Spot::MeshData BeveledBox(Spot::Vector3 size, float bevel, float uvRepeat);
// Standing along Y.
Spot::MeshData Cylinder(float radius, float height, std::uint32_t segments);
// A cylinder lying along X, as a wheel turns.
Spot::MeshData Wheel(float radius, float width, std::uint32_t segments);
// Standing along Y, with its tip up; its base is the bottom end.
Spot::MeshData Cone(float radius, float height, std::uint32_t segments);
// Steps rising toward -Z, from the ground at +Z, each as high and deep as given and as wide as the stairs. The texture
// repeats every uvRepeat units, as on a box.
Spot::MeshData Stairs(float width, float stepHeight, float stepDepth, int steps, float uvRepeat);
// A wedge rising toward -Z: its slope goes from the ground at +Z up to its full height at -Z. The texture repeats
// every uvRepeat units, as on a box.
Spot::MeshData Ramp(float width, float height, float length, float uvRepeat);
Spot::MeshData TargetDummy();

// A sphere of triangles, subdivided from an icosahedron so they are all about the same size; each vertex is moved
// out by the displacement function of its unit direction, as rocks and canopies are lumpy, and smooth normals follow.
Spot::MeshData Icosphere(float radius, int subdivisions, const std::function<float(Spot::Vector3)>& displacement = {});
// A cylinder narrowing from the bottom radius to the top one, standing along Y, centered like the others.
Spot::MeshData Frustum(float bottomRadius, float topRadius, float height, std::uint32_t segments);
// A profile in the YZ plane pushed along X for the length, centered on X, with caps at both ends. The profile goes
// around counterclockwise with Z to the right and Y up; caps fan out from its first point, which must see every other
// one. Points marked smooth share their two sides' normal, as along a curve.
struct ProfilePoint {
    float z;
    float y;
    bool smooth = false;
};
Spot::MeshData Extrude(std::span<const ProfilePoint> profile, float length, float uvRepeat);

// Adds the mesh, moved and turned by the transform, which must not scale, to the end of another, as models made of
// several shapes are built.
void Append(Spot::MeshData& to, const Spot::MeshData& from, const Spot::Matrix4& transform);

} // namespace Generator::Shapes
