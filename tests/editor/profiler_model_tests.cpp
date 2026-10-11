#include "core/profiler_model.h"
#include "editor/headless_imgui.h"
#include "panels/profiler_panel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h>

TEST_CASE("Profiler history retains selected frames while paused and averages scopes per frame", "[editor][profiler]") {
    Spot::Editor::ProfilerModel model;
    Spot::ProfileFrame first;
    first.number = 1;
    first.samples = {{"Physics", 0, {}, std::chrono::milliseconds(8)},
                     {"Physics", 0, {}, std::chrono::milliseconds(4)}};
    Spot::ProfileFrame second;
    second.number = 2;
    second.samples = {{"Physics", 0, {}, std::chrono::milliseconds(2)}};
    const Spot::ProfileFrame* frames[] = {&first, &second};
    model.Update(frames);
    REQUIRE(model.GetFrames().size() == 2);
    const auto averages = model.GetAverages();
    REQUIRE(averages.size() == 1);
    CHECK(averages[0].milliseconds == Catch::Approx(7.0));
    CHECK(averages[0].callsPerFrame == Catch::Approx(1.5));
    model.SetPaused(true);
    first.samples.clear();
    second.number = 3;
    model.Update(frames);
    CHECK(model.GetFrames().back().number == 2);
    CHECK(model.GetFrames().front().samples.size() == 2);
    model.SetPaused(false);
    model.Update(frames);
    CHECK(model.GetFrames().back().number == 3);
}

TEST_CASE("Profiler history bounds its ring and handles a restarted process", "[editor][profiler]") {
    Spot::Editor::ProfilerModel model;
    Spot::ProfileFrame frame;
    const Spot::ProfileFrame* frames[] = {nullptr, &frame};
    for (std::uint64_t i = 1; i <= Spot::Profiler::MaxFrames + 10; ++i) {
        frame.number = i;
        model.Update(frames);
    }
    CHECK(model.GetFrames().size() == Spot::Profiler::MaxFrames);
    CHECK(model.GetFrames().front().number == 11);
    frame.number = 1;
    model.Update(frames);
    REQUIRE(model.GetFrames().size() == 1);
    CHECK(model.GetFrames().back().number == 1);
    const Spot::ProfileFrame* empty[] = {nullptr};
    model.Update(empty);
    CHECK(model.GetFrames().size() == 1);
}

TEST_CASE("The profiler panel draws a recorded spike without a GPU", "[editor][profiler]") {
    Spot::Tests::HeadlessImGui ui;
    Spot::Editor::ProfilerPanel panel;
    Spot::ProfileFrame frame;
    frame.number = 7;
    frame.duration = std::chrono::milliseconds(40);
    frame.samples = {{"Physics spike", 0, {}, std::chrono::milliseconds(35)}};
    const Spot::ProfileFrame* frames[] = {&frame};
    panel.GetModel().Update(frames);
    panel.GetModel().SetPaused(true);
    ui.SetDraw([&] { panel.Draw(); });
    ui.Frame();
    REQUIRE(panel.GetModel().GetFrames().size() == 1);
    CHECK(panel.GetModel().GetAverages().front().name == "Physics spike");
}

TEST_CASE("Rolling profiler averages discard expired scopes and clear all totals on restart", "[editor][profiler]") {
    Spot::Editor::ProfilerModel model;
    Spot::ProfileFrame frame;
    frame.number = 1;
    frame.samples = {{"Expired", 0, {}, std::chrono::milliseconds(100)}};
    const Spot::ProfileFrame* frames[] = {&frame};
    model.Update(frames);
    frame.samples = {{"Current", 0, {}, std::chrono::milliseconds(2)}};
    for (std::uint64_t i = 2; i <= Spot::Profiler::MaxFrames + 1; ++i) {
        frame.number = i;
        model.Update(frames);
    }
    const auto averages = model.GetAverages();
    REQUIRE(averages.size() == 1);
    CHECK(averages.front().name == "Current");
    CHECK(averages.front().milliseconds == Catch::Approx(2.0));
    CHECK(averages.front().callsPerFrame == Catch::Approx(1.0));
    frame.number = 0;
    frame.samples.clear();
    model.Update(frames);
    CHECK(model.GetAverages().empty());
    model.Clear();
    CHECK(model.GetFrames().empty());
    CHECK(model.GetAverages().empty());
}

TEST_CASE("Clicking a profiler frame pauses collection so a spike can be inspected", "[editor][profiler]") {
    Spot::Tests::HeadlessImGui ui;
    Spot::Profiler::Clear();
    Spot::Editor::ProfilerPanel panel;
    Spot::ProfileFrame frame;
    frame.number = 7;
    frame.duration = std::chrono::milliseconds(40);
    frame.samples = {{"Physics spike", 0, {}, std::chrono::milliseconds(35)}};
    const Spot::ProfileFrame* frames[] = {&frame};
    panel.GetModel().Update(frames);
    ui.SetDraw([&] {
        ImGui::SetNextWindowSize({800.0f, 500.0f}, ImGuiCond_Always);
        panel.Draw();
    });
    ui.Frame();
    ui.Frame();
    auto* window = ImGui::FindWindowByName(panel.GetWindowName().c_str());
    REQUIRE(window);
    const auto origin = window->WorkRect.Min;
    const float plotY = origin.y + ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing();
    CHECK_FALSE(panel.GetModel().IsPaused());
    ui.Click({origin.x + 120.0f, plotY + 30.0f});
    CHECK(panel.GetModel().IsPaused());
    CHECK(panel.GetModel().GetFrames().back().number == 7);
}
