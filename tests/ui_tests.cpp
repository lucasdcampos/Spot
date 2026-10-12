#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

TEST_CASE("UI rects resolve fixed anchors stretch pivots and offsets", "[ui]") {
    Spot::UiRectLayout layout;
    const Spot::UiBounds parent{{10, 20}, {810, 620}};
    CHECK(Spot::ComputeUiRect(layout, parent) == Spot::UiBounds{{360, 305}, {460, 335}});
    layout.anchorMin = {};
    layout.anchorMax = {1, 1};
    layout.sizeDelta = {-20, -40};
    CHECK(Spot::ComputeUiRect(layout, parent) == Spot::UiBounds{{20, 40}, {800, 600}});
    layout.anchorMin = layout.anchorMax = {1, 1};
    layout.pivot = {1, 1};
    layout.offset = {-10, -10};
    layout.sizeDelta = {100, 50};
    CHECK(Spot::ComputeUiRect(layout, parent) == Spot::UiBounds{{700, 560}, {800, 610}});
    layout.sizeDelta = {-100, -50};
    CHECK(Spot::ComputeUiRect(layout, parent).IsEmpty());
}

TEST_CASE("Canvas scaling is finite and preserves aspect ratios", "[ui]") {
    Spot::UiCanvasSettings settings;
    CHECK(Spot::ComputeUiScale(settings, {2560, 1440}) == 1);
    settings.scaleMode = Spot::UiScaleMode::ReferenceResolution;
    CHECK(Spot::ComputeUiScale(settings, {2560, 1440}) == 2);
    CHECK(Spot::ComputeUiScale(settings, {1280, 1440}) == 1);
    settings.scale = std::numeric_limits<float>::quiet_NaN();
    settings.referenceResolution = {};
    CHECK(std::isfinite(Spot::ComputeUiScale(settings, {1280, 720})));
}

TEST_CASE("Canvas layout follows hierarchy and ignores world transforms", "[ui][scene]") {
    Spot::Scene scene;
    auto& root = scene.CreateEntity("UI");
    root.AddComponent<Spot::Canvas>().settings.scale = 2;
    auto& panel = root.CreateChild("Panel");
    panel.AddComponent<Spot::UiRect>().layout.sizeDelta = {200, 100};
    auto& label = panel.CreateChild("Label");
    label.AddComponent<Spot::UiRect>();
    label.GetTransform().SetPosition({1000, 1000, 1000});
    auto& system = scene.AddSystem<Spot::UiSystem>();
    system.Layout({800, 600});
    REQUIRE(system.GetEntries().size() == 3);
    CHECK(system.Find(panel.GetId())->bounds == Spot::UiBounds{{200, 200}, {600, 400}});
    CHECK(system.Find(label.GetId())->bounds == Spot::UiBounds{{300, 270}, {500, 330}});
    system.Layout({1600, 1200});
    CHECK(system.Find(panel.GetId())->bounds == Spot::UiBounds{{600, 500}, {1000, 700}});
    label.SetParent(&root);
    label.GetComponent<Spot::UiRect>()->layout.anchorMin = {};
    label.GetComponent<Spot::UiRect>()->layout.anchorMax = {};
    system.Layout({800, 600});
    CHECK(system.Find(label.GetId())->bounds == Spot::UiBounds{{-100, -30}, {100, 30}});
    label.SetActive(false);
    system.Layout({800, 600});
    CHECK_FALSE(system.Find(label.GetId()));
    system.Layout({});
    CHECK(system.GetEntries().empty());
}

TEST_CASE("Canvas order sibling order and serialization are stable", "[ui][scene]") {
    Spot::Scene scene;
    auto& front = scene.CreateEntity("Front");
    front.AddComponent<Spot::Canvas>().settings.order = 5;
    auto& back = scene.CreateEntity("Back");
    back.AddComponent<Spot::Canvas>().settings.scaleMode = Spot::UiScaleMode::ReferenceResolution;
    auto& first = back.CreateChild("First");
    first.AddComponent<Spot::UiRect>();
    auto& last = back.CreateChild("Last");
    last.AddComponent<Spot::UiRect>();
    last.SetSiblingIndex(0);
    auto& system = scene.AddSystem<Spot::UiSystem>();
    system.Layout({1280, 720});
    CHECK(system.GetEntries()[0].entity == back.GetId());
    CHECK(system.GetEntries()[1].entity == last.GetId());
    CHECK(system.GetEntries()[2].entity == first.GetId());
    CHECK(system.GetEntries()[3].entity == front.GetId());
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    const auto* canvas = loaded.FindEntity("Back")->GetComponent<Spot::Canvas>();
    REQUIRE(canvas);
    CHECK(canvas->settings.scaleMode == Spot::UiScaleMode::ReferenceResolution);
    REQUIRE(loaded.FindEntity("First")->GetComponent<Spot::UiRect>());
}
