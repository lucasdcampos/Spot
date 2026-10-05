#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <format>
#include <string>

using Spot::Matrix4;
using Spot::Pi;
using Spot::Quaternion;
using Spot::Vector2;
using Spot::Vector3;
using Spot::Vector4;

namespace {

constexpr float Tolerance = 1e-5f;

bool IsNear(Vector3 a, Vector3 b) {
    return std::abs(a.x - b.x) < Tolerance && std::abs(a.y - b.y) < Tolerance && std::abs(a.z - b.z) < Tolerance;
}

bool IsNear(Vector4 a, Vector4 b) {
    return IsNear(Vector3{a.x, a.y, a.z}, Vector3{b.x, b.y, b.z}) && std::abs(a.w - b.w) < Tolerance;
}

bool IsNear(const Matrix4& a, const Matrix4& b) {
    for (int column = 0; column < 4; ++column) {
        if (!IsNear(a.columns[column], b.columns[column])) {
            return false;
        }
    }
    return true;
}

Vector3 TransformPoint(const Matrix4& m, Vector3 point) {
    const Vector4 result = m * Vector4{point.x, point.y, point.z, 1.0f};
    return Vector3{result.x, result.y, result.z} / result.w;
}

} // namespace

namespace Catch {

template <>
struct StringMaker<Vector3> {
    static std::string convert(Vector3 v) { return std::format("({}, {}, {})", v.x, v.y, v.z); }
};

template <>
struct StringMaker<Vector4> {
    static std::string convert(Vector4 v) { return std::format("({}, {}, {}, {})", v.x, v.y, v.z, v.w); }
};

} // namespace Catch

TEST_CASE("Vectors support component-wise arithmetic", "[math]") {
    constexpr Vector3 a{1.0f, 2.0f, 3.0f};
    constexpr Vector3 b{4.0f, 5.0f, 6.0f};

    STATIC_CHECK(a + b == Vector3{5.0f, 7.0f, 9.0f});
    STATIC_CHECK(b - a == Vector3{3.0f, 3.0f, 3.0f});
    STATIC_CHECK(-a == Vector3{-1.0f, -2.0f, -3.0f});
    STATIC_CHECK(a * b == Vector3{4.0f, 10.0f, 18.0f});
    STATIC_CHECK(a * 2.0f == 2.0f * a);
    STATIC_CHECK(b / 2.0f == Vector3{2.0f, 2.5f, 3.0f});
    STATIC_CHECK(Vector2{1.0f, 2.0f} + Vector2{3.0f, 4.0f} == Vector2{4.0f, 6.0f});
    STATIC_CHECK(Vector4{1, 2, 3, 4} * 2.0f == Vector4{2, 4, 6, 8});

    Vector3 c = a;
    c += b;
    c -= a;
    c *= 2.0f;
    c /= 4.0f;
    CHECK(c == Vector3{2.0f, 2.5f, 3.0f});
}

TEST_CASE("Vector products, lengths and interpolation", "[math]") {
    STATIC_CHECK(Spot::Dot(Vector3{1, 2, 3}, Vector3{4, 5, 6}) == 32.0f);
    STATIC_CHECK(Spot::Cross(Vector3{1, 0, 0}, Vector3{0, 1, 0}) == Vector3{0, 0, 1});
    STATIC_CHECK(Spot::LengthSquared(Vector2{3, 4}) == 25.0f);
    STATIC_CHECK(Spot::Lerp(Vector2{0, 10}, Vector2{10, 20}, 0.25f) == Vector2{2.5f, 12.5f});
    STATIC_CHECK(Spot::Lerp(2.0f, 4.0f, 0.5f) == 3.0f);

    CHECK(Spot::Length(Vector2{3, 4}) == 5.0f);
    CHECK(Spot::Distance(Vector3{1, 1, 1}, Vector3{1, 4, 5}) == 5.0f);
    CHECK(IsNear(Spot::Normalize(Vector3{0, 3, 4}), Vector3{0, 0.6f, 0.8f}));
    CHECK(Spot::Normalize(Vector3{}) == Vector3{});
}

TEST_CASE("Angles convert between degrees and radians", "[math]") {
    STATIC_CHECK(Spot::ToRadians(180.0f) == Pi);
    CHECK(Spot::ToDegrees(Pi / 2.0f) == Catch::Approx(90.0f));
}

TEST_CASE("Quaternions rotate vectors right-handed", "[math]") {
    const auto yaw = Quaternion::FromAxisAngle({0, 1, 0}, Pi / 2.0f);

    CHECK(IsNear(yaw * Vector3{1, 0, 0}, Vector3{0, 0, -1}));
    CHECK(IsNear(Quaternion::FromEuler({0, Pi / 2.0f, 0}) * Vector3{1, 0, 0}, Vector3{0, 0, -1}));
    CHECK(IsNear(Quaternion::FromAxisAngle({0, 0, 5}, Pi / 2.0f) * Vector3{1, 0, 0}, Vector3{0, 1, 0}));
    CHECK(Quaternion::Identity() * Vector3{1, 2, 3} == Vector3{1, 2, 3});
}

TEST_CASE("Quaternions compose, invert and interpolate", "[math]") {
    const auto yaw = Quaternion::FromAxisAngle({0, 1, 0}, Pi / 2.0f);
    const auto pitch = Quaternion::FromAxisAngle({1, 0, 0}, Pi / 2.0f);
    const Vector3 v{0, 0, -1};

    CHECK(IsNear((yaw * pitch) * v, yaw * (pitch * v)));
    CHECK(IsNear(Spot::Inverse(yaw) * (yaw * v), v));

    const auto halfway = Spot::Slerp(Quaternion::Identity(), yaw, 0.5f);
    const float diagonal = std::sqrt(0.5f);
    CHECK(IsNear(halfway * Vector3{1, 0, 0}, Vector3{diagonal, 0, -diagonal}));

    const Vector3 angles{0.3f, -0.5f, 0.7f};
    CHECK(IsNear(Spot::ToEuler(Quaternion::FromEuler(angles)), angles));
    CHECK(Spot::ToEuler(Quaternion::FromAxisAngle({0, 0, 1}, 2.0f)).z == Catch::Approx(2.0f));

    const Quaternion scaled{0, 0, 0, 2};
    CHECK(Spot::Normalize(scaled) == Quaternion::Identity());
}

TEST_CASE("Matrices default to the identity and store columns contiguously", "[math]") {
    const Matrix4 identity;
    CHECK(identity == Matrix4::Identity());
    CHECK(identity * Vector4{1, 2, 3, 1} == Vector4{1, 2, 3, 1});

    const auto translation = Matrix4::Translation({5, 6, 7});
    const float* data = translation.GetData();
    CHECK(data[0] == 1.0f);
    CHECK(data[12] == 5.0f);
    CHECK(data[13] == 6.0f);
    CHECK(data[14] == 7.0f);
}

TEST_CASE("Matrix transforms combine right to left", "[math]") {
    const auto translate = Matrix4::Translation({10, 0, 0});
    const auto scale = Matrix4::Scale({2, 2, 2});
    const auto rotate = Matrix4::Rotation(Quaternion::FromAxisAngle({0, 0, 1}, Pi / 2.0f));

    CHECK(IsNear(TransformPoint(translate * scale, {1, 1, 1}), Vector3{12, 2, 2}));
    CHECK(IsNear(TransformPoint(scale * translate, {1, 1, 1}), Vector3{22, 2, 2}));
    CHECK(IsNear(TransformPoint(rotate, {1, 0, 0}), Vector3{0, 1, 0}));
}

TEST_CASE("Matrices invert and transpose", "[math]") {
    const auto transform = Matrix4::Translation({1, 2, 3}) *
                           Matrix4::Rotation(Quaternion::FromEuler({0.3f, 0.5f, 0.7f})) * Matrix4::Scale({2, 3, 4});

    CHECK(IsNear(Spot::Inverse(transform) * transform, Matrix4::Identity()));

    const auto transposed = Spot::Transpose(Matrix4::Translation({5, 6, 7}));
    CHECK(transposed.columns[0].w == 5.0f);
    CHECK(transposed.columns[1].w == 6.0f);
    CHECK(transposed.columns[2].w == 7.0f);
}

TEST_CASE("Perspective maps the view frustum to zero-to-one depth", "[math]") {
    const auto projection = Matrix4::Perspective(Spot::ToRadians(90.0f), 2.0f, 0.1f, 100.0f);

    CHECK(TransformPoint(projection, {0, 0, -0.1f}).z == Catch::Approx(0.0f).margin(Tolerance));
    CHECK(TransformPoint(projection, {0, 0, -100.0f}).z == Catch::Approx(1.0f));
    // A 90 degree vertical field of view puts the top edge at y == distance, and the aspect ratio widens x.
    CHECK(IsNear(TransformPoint(projection, {2, 1, -1}), Vector3{1, 1, TransformPoint(projection, {0, 0, -1}).z}));
}

TEST_CASE("Orthographic maps the box to clip space", "[math]") {
    const auto projection = Matrix4::Orthographic(0, 800, 0, 600, 0, 10);

    CHECK(IsNear(TransformPoint(projection, {0, 0, 0}), Vector3{-1, -1, 0}));
    CHECK(IsNear(TransformPoint(projection, {800, 600, -10}), Vector3{1, 1, 1}));
}

TEST_CASE("LookAt moves the eye to the origin looking down -Z", "[math]") {
    const auto view = Matrix4::LookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});

    CHECK(IsNear(TransformPoint(view, {0, 0, 0}), Vector3{0, 0, -5}));
    CHECK(IsNear(TransformPoint(view, {1, 2, 5}), Vector3{1, 2, 0}));
}
