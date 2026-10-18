#include "graphics/shadow_views.h"

#include "math/angles.h"
#include "math/quaternion.h"

#include <algorithm>
#include <cmath>

namespace Spot::Detail {

namespace {

// Cube faces and spot cones are widened by this many texels on each side, past the reach of shadow filtering.
constexpr float FilterMargin = 3.0f;

// An up direction that is not parallel to the direction.
Vector3 GetUpFor(Vector3 direction) {
    return std::abs(direction.y) > 0.99f * Length(direction) ? Vector3{0.0f, 0.0f, 1.0f} : Vector3{0.0f, 1.0f, 0.0f};
}

Vector4 GetRow(const Matrix4& m, int row) {
    const auto at = [&](int column) {
        const Vector4& c = m.columns[static_cast<std::size_t>(column)];
        return row == 0 ? c.x : row == 1 ? c.y : row == 2 ? c.z : c.w;
    };
    return {at(0), at(1), at(2), at(3)};
}

ShadowView GetLocalShadowView(Vector3 position, Vector3 direction, float tanHalfFov, float range,
                              std::uint32_t mapSize) {
    const Matrix4 view = Matrix4::LookAt(position, position + direction, GetUpFor(direction));
    const Matrix4 projection =
        Matrix4::Perspective(2.0f * std::atan(tanHalfFov), 1.0f, GetLocalShadowNearPlane(range), range);
    return {.viewProjection = projection * view, .texelSize = 2.0f * tanHalfFov / static_cast<float>(mapSize)};
}

} // namespace

std::array<float, MaxShadowCascades> GetCascadeEnds(float nearPlane, float shadowDistance, std::uint32_t count) {
    count = std::clamp(count, 1u, MaxShadowCascades);
    nearPlane = std::max(nearPlane, 0.001f);
    shadowDistance = std::max(shadowDistance, nearPlane * 1.001f);
    // Blends even and logarithmic splits (Zhang et al.'s practical split scheme): logarithmic ones alone would leave
    // the first cascade tiny with the near planes cameras use.
    constexpr float Logarithmic = 0.75f;
    std::array<float, MaxShadowCascades> ends{};
    for (std::uint32_t i = 0; i < MaxShadowCascades; ++i) {
        if (i + 1 >= count) {
            ends[i] = shadowDistance;
            continue;
        }
        const float share = static_cast<float>(i + 1) / static_cast<float>(count);
        const float logarithmic = nearPlane * std::pow(shadowDistance / nearPlane, share);
        const float even = nearPlane + (shadowDistance - nearPlane) * share;
        ends[i] = Logarithmic * logarithmic + (1.0f - Logarithmic) * even;
    }
    return ends;
}

ShadowView GetCascadeView(const Camera3D& camera, float aspectRatio, float nearDistance, float farDistance,
                          Vector3 lightDirection, std::uint32_t mapSize) {
    // The slice of the view is symmetric around the camera's forward axis, so the smallest sphere around its corners
    // is centered on that axis, where the near and far corners are equally far, unless that is past the far plane.
    const float tanY = std::tan(camera.fieldOfView * 0.5f);
    const float tanX = tanY * aspectRatio;
    const float spread = tanX * tanX + tanY * tanY;
    const float centerDistance = std::min((farDistance + nearDistance) * (1.0f + spread) * 0.5f, farDistance);
    float radius =
        std::sqrt((farDistance - centerDistance) * (farDistance - centerDistance) + farDistance * farDistance * spread);
    // Rounding keeps the size, and so the texels, from changing at every small change of the slice.
    radius = std::ceil(radius * 16.0f) / 16.0f;
    const Vector3 center = camera.position + camera.GetForward() * centerDistance;

    // The light's view only rotates, so moving in the world moves as much in it, and its texels stay on a fixed grid.
    const Quaternion rotation = Quaternion::LookRotation(lightDirection, GetUpFor(lightDirection));
    const Matrix4 view = Matrix4::Rotation(Inverse(rotation));
    Vector3 viewCenter = Inverse(rotation) * center;
    const float texelSize = 2.0f * radius / static_cast<float>(mapSize);
    viewCenter.x = std::floor(viewCenter.x / texelSize) * texelSize;
    viewCenter.y = std::floor(viewCenter.y / texelSize) * texelSize;

    const Matrix4 projection =
        Matrix4::Orthographic(viewCenter.x - radius, viewCenter.x + radius, viewCenter.y - radius,
                              viewCenter.y + radius, -viewCenter.z - radius, -viewCenter.z + radius);
    return {.viewProjection = projection * view, .texelSize = texelSize};
}

ShadowView GetSpotShadowView(const LightData& light, std::uint32_t mapSize) {
    const float size = static_cast<float>(mapSize);
    const float outer = std::clamp(light.outerConeAngle, 0.001f, ToRadians(80.0f));
    const float tanHalfFov = std::tan(outer) * size / (size - 2.0f * FilterMargin);
    return GetLocalShadowView(light.position, Normalize(light.direction), tanHalfFov, light.range, mapSize);
}

ShadowView GetPointShadowView(const LightData& light, std::uint32_t face, std::uint32_t mapSize) {
    constexpr std::array<Vector3, 6> Axes{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    const float size = static_cast<float>(mapSize);
    const float tanHalfFov = size / (size - 2.0f * FilterMargin);
    return GetLocalShadowView(light.position, Axes[std::min<std::size_t>(face, 5)], tanHalfFov, light.range, mapSize);
}

float GetLocalShadowNearPlane(float range) {
    return std::max(range * 0.005f, 0.01f);
}

Frustum::Frustum(const Matrix4& viewProjection, bool ignoreNear) : m_planeCount(ignoreNear ? 5 : 6) {
    // The planes of clip space, from the rows of the matrix (Gribb and Hartmann), for depth in [0, 1].
    const Vector4 x = GetRow(viewProjection, 0);
    const Vector4 y = GetRow(viewProjection, 1);
    const Vector4 z = GetRow(viewProjection, 2);
    const Vector4 w = GetRow(viewProjection, 3);
    m_planes = {w + x, w - x, w + y, w - y, w - z, z};
    for (Vector4& plane : m_planes) {
        const float length = Length(Vector3{plane.x, plane.y, plane.z});
        // Degenerate planes reject nothing.
        plane = length > 0.0f ? plane * (1.0f / length) : Vector4{0.0f, 0.0f, 0.0f, 1.0f};
    }
    if (ignoreNear) {
        m_planes[5] = {0.0f, 0.0f, 0.0f, 1.0f};
    }
}

bool Frustum::Contains(Vector3 center, float radius) const {
    for (std::size_t i = 0; i < m_planeCount; ++i) {
        const Vector4& plane = m_planes[i];
        if (plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w < -radius) {
            return false;
        }
    }
    return true;
}

bool IsSphereInView(const Matrix4& viewProjection, Vector3 center, float radius, bool ignoreNear) {
    return Frustum(viewProjection, ignoreNear).Contains(center, radius);
}

} // namespace Spot::Detail
