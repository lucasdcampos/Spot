#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>

TEST_CASE("Horizontal and vertical groups measure padding and arrange sibling order", "[ui][layout]") {
    Spot::UiLayoutGroupSettings settings;
    settings.padding = {10, 10, 10, 10};
    settings.spacing = {5, 5};
    settings.direction = Spot::UiLayoutDirection::Horizontal;
    const std::array sizes{Spot::Vector2{20, 10}, Spot::Vector2{30, 20}};
    CHECK(Spot::MeasureUiLayoutGroup(settings, sizes) == Spot::Vector2{75, 40});
    auto rects = Spot::ArrangeUiLayoutGroup(settings, {{0, 0}, {100, 100}}, sizes);
    REQUIRE(rects.size() == 2);
    CHECK(rects[0] == Spot::UiBounds{{10, 80}, {30, 90}});
    CHECK(rects[1] == Spot::UiBounds{{35, 70}, {65, 90}});
    settings.direction = Spot::UiLayoutDirection::Vertical;
    CHECK(Spot::MeasureUiLayoutGroup(settings, sizes) == Spot::Vector2{50, 55});
    rects = Spot::ArrangeUiLayoutGroup(settings, {{0, 0}, {100, 100}}, sizes);
    CHECK(rects[0] == Spot::UiBounds{{10, 80}, {30, 90}});
    CHECK(rects[1] == Spot::UiBounds{{10, 55}, {40, 75}});
    settings.expandWidth = settings.expandHeight = true;
    rects = Spot::ArrangeUiLayoutGroup(settings, {{0, 0}, {100, 100}}, sizes);
    CHECK(rects[0].GetSize().x == 80);
    CHECK(rects[0].GetSize().y == Catch::Approx(37.5f));
}

TEST_CASE("Grid groups arrange rows and fitters measure nested groups", "[ui][layout]") {
    Spot::UiLayoutGroupSettings settings;
    settings.padding = {};
    settings.direction = Spot::UiLayoutDirection::Grid;
    settings.cellSize = {20, 10};
    settings.spacing = {2, 3};
    settings.columns = 2;
    const std::array sizes{Spot::Vector2{}, Spot::Vector2{}, Spot::Vector2{}};
    CHECK(Spot::MeasureUiLayoutGroup(settings, sizes) == Spot::Vector2{42, 23});
    const auto rects = Spot::ArrangeUiLayoutGroup(settings, {{0, 0}, {100, 100}}, sizes);
    CHECK(rects[0] == Spot::UiBounds{{0, 90}, {20, 100}});
    CHECK(rects[1] == Spot::UiBounds{{22, 90}, {42, 100}});
    CHECK(rects[2] == Spot::UiBounds{{0, 77}, {20, 87}});
    Spot::Scene scene;
    auto& root = scene.CreateEntity("Canvas");
    root.AddComponent<Spot::Canvas>();
    auto& panel = root.CreateChild("Panel");
    panel.AddComponent<Spot::UiRect>();
    auto& group = panel.AddComponent<Spot::UiLayoutGroup>();
    group.settings = settings;
    panel.AddComponent<Spot::UiSizeFitter>();
    for (int i = 0; i < 3; ++i)
        panel.CreateChild("Item").AddComponent<Spot::UiRect>();
    auto& system = scene.AddSystem<Spot::UiSystem>();
    system.Layout({800, 600});
    REQUIRE(system.Find(panel.GetId()));
    CHECK(system.Find(panel.GetId())->bounds.GetSize() == Spot::Vector2{42, 23});
    panel.GetChildren()[1]->SetActive(false);
    system.Layout({800, 600});
    CHECK(system.Find(panel.GetId())->bounds.GetSize() == Spot::Vector2{42, 10});
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    CHECK(loaded.FindComponent<Spot::UiLayoutGroup>()->settings.direction == Spot::UiLayoutDirection::Grid);
    CHECK(loaded.FindComponent<Spot::UiSizeFitter>()->fitWidth);
}
