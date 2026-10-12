#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

std::optional<Spot::FontAtlas> LoadTestAtlas(float pixelSize) {
    std::ifstream file(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "fonts" / "FiraSans-Regular.ttf",
                       std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return Spot::FontAtlas::Create(std::as_bytes(std::span(bytes)), pixelSize);
}

} // namespace

TEST_CASE("UTF-8 decodes to code points of every length", "[text]") {
    CHECK(Spot::DecodeUtf8("A") == U"A");
    // "é" (2 bytes), "€" (3 bytes) and an emoji (4 bytes), spelled out as bytes.
    CHECK(Spot::DecodeUtf8("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80") == std::u32string{0xE9, 0x20AC, 0x1F600});
    CHECK(Spot::DecodeUtf8("").empty());
}

TEST_CASE("Invalid UTF-8 becomes replacement characters without losing the rest", "[text]") {
    const char32_t bad = Spot::ReplacementCharacter;

    // A stray continuation byte, a truncated sequence, and an overlong encoding of '/'.
    CHECK(Spot::DecodeUtf8("a\x80z") == std::u32string{U'a', bad, U'z'});
    CHECK(Spot::DecodeUtf8("a\xC3") == std::u32string{U'a', bad});
    CHECK(Spot::DecodeUtf8("\xC0\xAF!") == std::u32string{bad, bad, U'!'});
}

TEST_CASE("FontAtlas rasterizes ASCII and Latin-1 glyphs", "[text]") {
    const auto atlas = LoadTestAtlas(32.0f);
    REQUIRE(atlas.has_value());

    CHECK(atlas->GetPixelSize() == 32.0f);
    CHECK(atlas->GetAscent() > 0.0f);
    CHECK(atlas->GetLineHeight() >= atlas->GetAscent());

    const auto* a = atlas->FindGlyph(U'A');
    REQUIRE(a != nullptr);
    CHECK(a->advance > 0.0f);
    CHECK(a->max.x > a->min.x);
    CHECK(a->max.y > a->min.y);
    CHECK(a->uvMax.x > a->uvMin.x);

    const auto* question = atlas->FindGlyph(U'?');
    CHECK(atlas->FindGlyph(U'ç') != question);
    // Characters outside the atlas fall back to '?'.
    CHECK(atlas->FindGlyph(U'中') == question);

    const auto& image = atlas->GetImage();
    CHECK(image.GetWidth() == image.GetHeight());
    CHECK(image.GetPixels().size() == static_cast<std::size_t>(image.GetWidth()) * image.GetHeight() * 4);
}

TEST_CASE("FontAtlas reports unreadable font data", "[text]") {
    const auto capture = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({capture});

    const std::array<std::byte, 16> garbage{};
    CHECK_FALSE(Spot::FontAtlas::Create(garbage, 16.0f).has_value());
    CHECK_FALSE(capture->GetRecords().empty());
}

TEST_CASE("Text is laid out left to right, line by line, below its top-left corner", "[text]") {
    const auto atlas = LoadTestAtlas(32.0f);
    REQUIRE(atlas.has_value());

    const auto quads = Spot::LayoutText(*atlas, "ab\ncd");
    REQUIRE(quads.size() == 4);
    CHECK(quads[1].min.x > quads[0].min.x);
    CHECK(quads[0].max.y <= 6.0f);
    CHECK(quads[2].min.x == Catch::Approx(quads[0].min.x).margin(1.0f));
    CHECK(quads[2].min.y < quads[0].min.y);

    const auto* a = atlas->FindGlyph(U'a');
    const auto* b = atlas->FindGlyph(U'b');
    const auto size = Spot::MeasureText(*atlas, "ab\ncd");
    CHECK(Spot::MeasureText(*atlas, "ab").x == Catch::Approx(a->advance + b->advance + atlas->GetKerning(U'a', U'b')));
    CHECK(size.y == Catch::Approx(2.0f * atlas->GetLineHeight()));
}

TEST_CASE("Spaces and tabs move the pen without adding glyphs", "[text]") {
    const auto atlas = LoadTestAtlas(32.0f);
    REQUIRE(atlas.has_value());
    const float space = atlas->FindGlyph(U' ')->advance;

    CHECK(Spot::LayoutText(*atlas, " \t ").empty());
    CHECK(Spot::MeasureText(*atlas, " ").x == Catch::Approx(space));
    CHECK(Spot::MeasureText(*atlas, "\t").x == Catch::Approx(4.0f * space));
}

TEST_CASE("QuadBatch maps texture regions to the quad corners", "[text]") {
    Spot::QuadBatch batch;
    batch.AddQuad({}, {1.0f, 1.0f}, 0.0f, nullptr, Spot::Color::White, {{0.25f, 0.5f}, {0.75f, 1.0f}});

    const auto vertices = batch.GetVertices();
    CHECK(vertices[0].uv == Spot::Vector2{0.25f, 0.5f});
    CHECK(vertices[1].uv == Spot::Vector2{0.75f, 0.5f});
    CHECK(vertices[2].uv == Spot::Vector2{0.75f, 1.0f});
    CHECK(vertices[3].uv == Spot::Vector2{0.25f, 1.0f});
}

TEST_CASE("SDF fonts encode distances and reject invalid sizes", "[text]") {
    const auto atlas = LoadTestAtlas(32);
    REQUIRE(atlas);
    bool inside = false;
    bool outside = false;
    bool edge = false;
    const auto pixels = atlas->GetImage().GetPixels();
    for (std::size_t i = 3; i < pixels.size(); i += 4) {
        inside = inside || pixels[i] > 150;
        outside = outside || pixels[i] < 100;
        edge = edge || (pixels[i] >= 120 && pixels[i] <= 135);
    }
    CHECK(inside);
    CHECK(outside);
    CHECK(edge);
    CHECK_FALSE(LoadTestAtlas(0));
    CHECK_FALSE(LoadTestAtlas(10000));
}

TEST_CASE("World text follows transforms and can face the camera", "[text][scene]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Label");
    auto& text = entity.AddComponent<Spot::TextRenderer>();
    entity.GetTransform().SetPosition({2, 3, 4});
    Spot::Camera3D camera;
    CHECK(text.GetDrawMatrix(camera) == entity.GetTransform().GetWorldMatrix());
    text.SetFacingCamera(true);
    entity.GetTransform().SetRotation(Spot::Quaternion::FromAxisAngle({1, 0, 0}, 0.8f));
    camera.rotation = Spot::Quaternion::FromAxisAngle({0, 1, 0}, 0.6f);
    CHECK(text.GetDrawMatrix(camera) ==
          Spot::Matrix4::Translation({2, 3, 4}) * Spot::Matrix4::Rotation(camera.rotation));
    CHECK(Spot::Decompose(text.GetDrawMatrix(camera)).translation == Spot::Vector3{2, 3, 4});
    text.SetPixelScale(-1);
    CHECK(text.GetPixelScale() == 0);
    CHECK(Spot::CreateComponent("TextRenderer"));
}

TEST_CASE("World text fields survive scene serialization", "[text][scene]") {
    Spot::Scene scene;
    auto& label = scene.CreateEntity("Label").AddComponent<Spot::TextRenderer>();
    label.SetText("Hello world");
    label.SetFacingCamera(true);
    label.SetPixelScale(0.25f);
    label.SetOrder(3);
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    const auto* copy = loaded.FindComponent<Spot::TextRenderer>();
    REQUIRE(copy);
    CHECK(copy->GetText() == "Hello world");
    CHECK(copy->IsFacingCamera());
    CHECK(copy->GetPixelScale() == 0.25f);
    CHECK(copy->GetOrder() == 3);
}
