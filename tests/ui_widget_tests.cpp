#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace {
std::optional<Spot::FontAtlas> Atlas() {
    std::ifstream stream(std::filesystem::path(SPOT_TEST_ASSETS_DIR) / "fonts/FiraSans-Regular.ttf", std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(stream), {}};
    return Spot::FontAtlas::Create(std::as_bytes(std::span(bytes)), 32);
}
} // namespace

TEST_CASE("Nine-slice preserves source borders and fits small rectangles", "[ui][widgets]") {
    const auto quads = Spot::BuildUiNineSlice({{0, 0}, {100, 60}}, {32, 32}, {8, 4, 8, 4});
    REQUIRE(quads.size() == 9);
    CHECK(quads[0].bounds.GetSize() == Spot::Vector2{8, 4});
    CHECK(quads[4].bounds.GetSize() == Spot::Vector2{84, 52});
    CHECK(quads[0].region.min == Spot::Vector2{0, 0.875f});
    CHECK(quads[0].region.max == Spot::Vector2{0.25f, 1});
    const auto tiny = Spot::BuildUiNineSlice({{0, 0}, {4, 2}}, {32, 32}, {8, 4, 8, 4});
    REQUIRE(tiny.size() == 4);
    for (const auto& quad : tiny) {
        CHECK(quad.bounds.min.x >= 0);
        CHECK(quad.bounds.max.x <= 4);
        CHECK(quad.bounds.min.y >= 0);
        CHECK(quad.bounds.max.y <= 2);
    }
}

TEST_CASE("UI text wraps words and long words aligns and ellipsizes", "[ui][widgets][text]") {
    const auto atlas = Atlas();
    REQUIRE(atlas);
    const float width = Spot::MeasureText(*atlas, "Hello").x + 1;
    auto wrapped = Spot::LayoutUiText(*atlas, "Hello world", {width, 300});
    CHECK(wrapped.size.y >= atlas->GetLineHeight() * 2);
    const auto longWord = Spot::LayoutUiText(*atlas, "AAAAAAAAAA", {32, 300});
    CHECK(longWord.size.y > atlas->GetLineHeight());
    const auto left = Spot::LayoutUiText(*atlas, "Hi", {200, 100});
    const auto center = Spot::LayoutUiText(*atlas, "Hi", {200, 100}, {.alignment = Spot::UiTextAlignment::Center});
    const auto right = Spot::LayoutUiText(*atlas, "Hi", {200, 100}, {.alignment = Spot::UiTextAlignment::Right});
    REQUIRE(left.glyphs.size() == 2);
    CHECK(center.glyphs[0].min.x - left.glyphs[0].min.x == Catch::Approx((200 - left.size.x) / 2));
    CHECK(right.glyphs[0].min.x - left.glyphs[0].min.x == Catch::Approx(200 - left.size.x));
    const auto clipped = Spot::LayoutUiText(*atlas, "one\ntwo\nthree", {200, atlas->GetLineHeight()});
    CHECK(clipped.glyphs.size() == 3);
    const auto ellipsis = Spot::LayoutUiText(*atlas, "Hello world", {width, 100},
                                             {.overflow = Spot::UiTextOverflow::Ellipsis, .wrap = false});
    REQUIRE(ellipsis.glyphs.size() >= 3);
    CHECK(ellipsis.size.x <= width);
    CHECK(ellipsis.glyphs.back().uvMin == atlas->FindGlyph(U'.')->uvMin);
}

TEST_CASE("UI control signals clamp values and contain callback failures", "[ui][widgets]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Controls");
    auto& button = entity.AddComponent<Spot::UiButton>();
    int clicks = 0;
    auto connection = button.OnClick.Connect([&] { ++clicks; });
    button.Click();
    CHECK(clicks == 1);
    button.SetInteractable(false);
    button.Click();
    CHECK(clicks == 1);
    auto& toggle = entity.AddComponent<Spot::UiToggle>();
    bool value = false;
    auto toggleConnection = toggle.OnValueChanged.Connect([&](bool changed) { value = changed; });
    toggle.HandleUiEvent({.type = Spot::UiEventType::Activate});
    CHECK(toggle.IsOn());
    CHECK(value);
    auto& slider = entity.AddComponent<Spot::UiSlider>();
    slider.minimum = 10;
    slider.maximum = 20;
    slider.SetValue(100);
    CHECK(slider.GetValue() == 20);
    slider.SetValue(std::numeric_limits<float>::quiet_NaN());
    CHECK(slider.GetValue() == 10);
    const auto capture = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({capture});
    button.SetInteractable(true);
    auto throwing = button.OnClick.Connect([] { throw std::runtime_error("Click failed"); });
    button.Click();
    CHECK(button.IsFaulted());
    button.Click();
    CHECK(clicks == 2);
    CHECK(capture->GetRecords().size() == 1);
}

TEST_CASE("Input fields edit complete UTF-8 code points at the caret", "[ui][widgets]") {
    Spot::Scene scene;
    auto& field = scene.CreateEntity("Field").AddComponent<Spot::UiInputField>();
    field.maxLength = 3;
    field.HandleUiEvent({.type = Spot::UiEventType::Text, .text = "a\xC3\xA9zq"});
    CHECK(Spot::DecodeUtf8(field.GetText()) == U"aéz");
    field.HandleUiEvent({.type = Spot::UiEventType::Left});
    field.HandleUiEvent({.type = Spot::UiEventType::Backspace});
    CHECK(field.GetText() == "az");
    CHECK(field.GetCaret() == 1);
    field.HandleUiEvent({.type = Spot::UiEventType::Text, .text = "\xF0\x9F\x98\x80"});
    CHECK(Spot::DecodeUtf8(field.GetText()) == std::u32string{U'a', 0x1F600, U'z'});
    field.HandleUiEvent({.type = Spot::UiEventType::Left});
    field.HandleUiEvent({.type = Spot::UiEventType::Delete});
    CHECK(field.GetText() == "az");
    field.HandleUiEvent({.type = Spot::UiEventType::Home});
    field.HandleUiEvent({.type = Spot::UiEventType::Delete});
    CHECK(field.GetText() == "z");
}

TEST_CASE("Scroll views offset children and inherit clipping through serialization", "[ui][widgets]") {
    Spot::Scene scene;
    auto& root = scene.CreateEntity("Canvas");
    root.AddComponent<Spot::Canvas>();
    auto& viewport = root.CreateChild("Scroll");
    viewport.AddComponent<Spot::UiRect>().layout.sizeDelta = {100, 100};
    auto& scroll = viewport.AddComponent<Spot::UiScrollView>();
    scroll.contentSize = {100, 400};
    auto& item = viewport.CreateChild("Item");
    auto& rect = item.AddComponent<Spot::UiRect>();
    rect.layout.anchorMin = rect.layout.anchorMax = {0, 1};
    rect.layout.pivot = {0, 1};
    item.AddComponent<Spot::UiImage>().tint = Spot::Color::Red;
    auto& system = scene.AddSystem<Spot::UiSystem>();
    system.Layout({800, 600});
    const auto original = system.Find(item.GetId())->bounds;
    scroll.SetOffset({0, 80});
    system.Layout({800, 600});
    CHECK(system.Find(item.GetId())->bounds.min.y == original.min.y + 80);
    CHECK(system.Find(item.GetId())->clip == system.Find(viewport.GetId())->bounds);
    scroll.SetOffset({0, 999});
    CHECK(scroll.GetOffset().y == 300);
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    CHECK(loaded.FindComponent<Spot::UiScrollView>()->GetOffset().y == 300);
    CHECK(loaded.FindComponent<Spot::UiImage>()->tint == Spot::Color::Red);
}

TEST_CASE("UI callbacks can destroy their entity safely", "[ui][widgets]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Button");
    const auto id = entity.GetId();
    auto& button = entity.AddComponent<Spot::UiButton>();
    auto connection = button.OnClick.Connect([&] { entity.Destroy(); });
    button.Click();
    CHECK_FALSE(scene.GetEntity(id));
}
