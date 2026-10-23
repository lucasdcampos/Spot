#pragma once

#include <cmath>
#include <concepts>

namespace Spot {

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;

    friend constexpr bool operator==(const Vector2&, const Vector2&) = default;
};

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    friend constexpr bool operator==(const Vector3&, const Vector3&) = default;
};

struct Vector4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    friend constexpr bool operator==(const Vector4&, const Vector4&) = default;
};

constexpr Vector2 operator+(Vector2 a, Vector2 b) {
    return {a.x + b.x, a.y + b.y};
}

constexpr Vector2 operator-(Vector2 a, Vector2 b) {
    return {a.x - b.x, a.y - b.y};
}

constexpr Vector2 operator-(Vector2 v) {
    return {-v.x, -v.y};
}

constexpr Vector2 operator*(Vector2 a, Vector2 b) {
    return {a.x * b.x, a.y * b.y};
}

constexpr Vector2 operator*(Vector2 v, float s) {
    return {v.x * s, v.y * s};
}

constexpr Vector2 operator*(float s, Vector2 v) {
    return v * s;
}

constexpr Vector2 operator/(Vector2 v, float s) {
    return {v.x / s, v.y / s};
}

constexpr Vector3 operator+(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

constexpr Vector3 operator-(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

constexpr Vector3 operator-(Vector3 v) {
    return {-v.x, -v.y, -v.z};
}

constexpr Vector3 operator*(Vector3 a, Vector3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

constexpr Vector3 operator*(Vector3 v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

constexpr Vector3 operator*(float s, Vector3 v) {
    return v * s;
}

constexpr Vector3 operator/(Vector3 v, float s) {
    return {v.x / s, v.y / s, v.z / s};
}

constexpr Vector4 operator+(Vector4 a, Vector4 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}

constexpr Vector4 operator-(Vector4 a, Vector4 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}

constexpr Vector4 operator-(Vector4 v) {
    return {-v.x, -v.y, -v.z, -v.w};
}

constexpr Vector4 operator*(Vector4 a, Vector4 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w};
}

constexpr Vector4 operator*(Vector4 v, float s) {
    return {v.x * s, v.y * s, v.z * s, v.w * s};
}

constexpr Vector4 operator*(float s, Vector4 v) {
    return v * s;
}

constexpr Vector4 operator/(Vector4 v, float s) {
    return {v.x / s, v.y / s, v.z / s, v.w / s};
}

template <typename T>
concept VectorType = std::same_as<T, Vector2> || std::same_as<T, Vector3> || std::same_as<T, Vector4>;

template <VectorType T>
constexpr T& operator+=(T& a, T b) {
    return a = a + b;
}

template <VectorType T>
constexpr T& operator-=(T& a, T b) {
    return a = a - b;
}

template <VectorType T>
constexpr T& operator*=(T& v, float s) {
    return v = v * s;
}

template <VectorType T>
constexpr T& operator/=(T& v, float s) {
    return v = v / s;
}

constexpr float Dot(Vector2 a, Vector2 b) {
    return a.x * b.x + a.y * b.y;
}

constexpr float Dot(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr float Dot(Vector4 a, Vector4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

constexpr Vector3 Cross(Vector3 a, Vector3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

template <VectorType T>
constexpr float LengthSquared(T v) {
    return Dot(v, v);
}

template <VectorType T>
float Length(T v) {
    return std::sqrt(Dot(v, v));
}

template <VectorType T>
float Distance(T a, T b) {
    return Length(b - a);
}

// A zero vector has no direction, so it stays zero instead of becoming NaN.
template <VectorType T>
T Normalize(T v) {
    const float length = Length(v);
    return length > 0.0f ? v / length : T{};
}

constexpr float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

template <VectorType T>
constexpr T Lerp(T a, T b, float t) {
    return a + (b - a) * t;
}

} // namespace Spot
