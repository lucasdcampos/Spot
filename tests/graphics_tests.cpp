#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Vertex formats report their size in bytes", "[graphics]") {
    CHECK(Spot::GetVertexFormatSize(Spot::VertexFormat::Float) == 4);
    CHECK(Spot::GetVertexFormatSize(Spot::VertexFormat::Float2) == 8);
    CHECK(Spot::GetVertexFormatSize(Spot::VertexFormat::Float3) == 12);
    CHECK(Spot::GetVertexFormatSize(Spot::VertexFormat::Float4) == 16);
    CHECK(Spot::GetVertexFormatSize(Spot::VertexFormat::UByte4Normalized) == 4);
}

TEST_CASE("Colors have opaque defaults and named constants", "[graphics]") {
    STATIC_CHECK(Spot::Color{} == Spot::Color::Black);
    STATIC_CHECK(Spot::Color::Transparent.a == 0.0f);
    STATIC_CHECK(Spot::Color::Red == Spot::Color{1.0f, 0.0f, 0.0f});
}

TEST_CASE("Colors convert from sRGB to linear", "[graphics]") {
    CHECK(Spot::Color::FromSrgb(0.0f, 1.0f, 0.5f, 0.5f) ==
          Spot::Color{0.0f, 1.0f, Spot::Color::FromSrgb(0.5f, 0.5f, 0.5f).b, 0.5f});
    CHECK(Spot::Color::FromSrgb(0.5f, 0.5f, 0.5f).r == Catch::Approx(0.214f).margin(0.001f));
    // The darkest values are scaled linearly instead of by the curve.
    CHECK(Spot::Color::FromSrgb(0.02f, 0.0f, 0.0f).r == Catch::Approx(0.02f / 12.92f));
    STATIC_CHECK(Spot::IsSrgbFormat(Spot::TextureFormat::Bgra8Srgb));
    STATIC_CHECK_FALSE(Spot::IsSrgbFormat(Spot::TextureFormat::Rgba8));
}
