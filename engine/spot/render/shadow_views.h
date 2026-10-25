#pragma once

#include "spot/core/math/matrix.h"
#include "spot/core/math/vector.h"
#include "spot/render/camera_3d.h"
#include "spot/render/light_data.h"

#include <array>
#include <cstdint>

namespace Spot::Detail {

// How Renderer3D draws one tile of a shadow map: what the light sees from it, as a view projection whose clip space
// the tile covers, and how large one of its texels is in the world, which shaders offset surfaces by so they do not
// shadow themselves.
struct ShadowView {
    Matrix4 viewProjection;
    // In world units for directional lights; per unit of distance from the light for the others, whose texels grow
    // with it.
    float texelSize = 0.0f;
};

inline constexpr std::uint32_t MaxShadowCascades = 4;

// The view distances at which each of the cascades of a directional light's shadows ends, from the camera's near
// plane to the shadow distance: closer together near the camera, where shadows need the most detail. Unused cascades
// end at the shadow distance.
std::array<float, MaxShadowCascades> GetCascadeEnds(float nearPlane, float shadowDistance, std::uint32_t count);

// A cascade covering what the camera sees between two view distances, from a light shining in the direction. It holds
// a sphere around that slice of the view, so its size does not change as the camera turns, and it moves in steps of
// whole texels, so shadow edges do not crawl as the camera moves. Its depth only spans that sphere: the passes that
// draw it clamp the depth of casters closer to the light instead of clipping them.
ShadowView GetCascadeView(const Camera3D& camera, float aspectRatio, float nearDistance, float farDistance,
                          Vector3 lightDirection, std::uint32_t mapSize);

// A spot light's cone, a little wider so filtering at its edge stays inside the tile.
ShadowView GetSpotShadowView(const LightData& light, std::uint32_t mapSize);

// One of the six faces of a cube around a point light, in the order +X, -X, +Y, -Y, +Z and -Z. Shaders pick the face
// by the axis a direction from the light points along most; each face sees a little past the cube's edges, so
// filtering there stays inside its tile.
ShadowView GetPointShadowView(const LightData& light, std::uint32_t face, std::uint32_t mapSize);

// The sky's occlusion around a position: a view straight down, along -Y with -Z up the map, distance to each side of
// the position and as far above and below it, which moves in whole texels, so what it finds under cover does not
// crawl as the position moves. Its depth grows downward, over twice the distance.
ShadowView GetSkyOcclusionView(Vector3 position, float distance, std::uint32_t mapSize);

// The near plane of shadow views of local lights, a share of their range.
float GetLocalShadowNearPlane(float range);

// The planes around what a view projection sees, normalized once, so many spheres can be tested against them. With
// ignoreNear, spheres in front of the near plane count, as casters of directional shadows do.
class Frustum {
public:
    explicit Frustum(const Matrix4& viewProjection, bool ignoreNear = false);

    // Whether a sphere may show in the view.
    bool Contains(Vector3 center, float radius) const;
    // As shaders test them: a plane that is not tested rejects nothing.
    const std::array<Vector4, 6>& GetPlanes() const { return m_planes; }

private:
    std::array<Vector4, 6> m_planes;
    std::size_t m_planeCount;
};

// Where the camera that picks levels of detail is, and how it sizes things, as the mesh shaders take it: a sphere's
// screen size is its radius times w over its distance, or times -w alone when w is negative, for orthographic cameras.
Vector4 GetLodView(const Camera3D& camera);

bool IsSphereInView(const Matrix4& viewProjection, Vector3 center, float radius, bool ignoreNear = false);

} // namespace Spot::Detail
