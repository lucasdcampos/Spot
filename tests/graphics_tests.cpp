#include <spot.h>

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
