#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr float Step = Spot::Scene::DefaultFixedTimeStep;

struct Log {
    std::shared_ptr<Spot::Tests::CaptureSink> sink = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks{{sink}};
    std::vector<std::string> Messages() const {
        std::vector<std::string> messages;
        for (const auto& record : sink->GetRecords()) {
            if (record.loggerName == "Luau")
                messages.push_back(record.message);
        }
        return messages;
    }
    bool Contains(std::string_view text) const {
        return std::ranges::any_of(sink->GetRecords(),
                                   [&](const auto& record) { return record.message.find(text) != std::string::npos; });
    }
};

Spot::ScriptComponent& AddScript(Spot::Entity& entity, std::string source) {
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "luau";
    script.sourceName = "test.luau";
    script.source = std::move(source);
    return script;
}

} // namespace

TEST_CASE("Luau exports top level globals with constant values and takes saved values before OnStart",
          "[luau][tools][export]") {
    Log log;
    const std::string source = R"(
        speed = 3
        label = "crate"
        offset = Vector3(0, 1, 0)
        tint = Color(1, 1, 1, 1)
        active = true
        depth = -(2)
        local hidden = 5
        computed = hidden * 2
        speed = 9
        function OnStart() print(speed, label, offset.y, tint.g, active, depth) end
    )";
    std::string saved;
    {
        Spot::Scene scene;
        auto& script = AddScript(scene.CreateEntity("Crate"), source);
        const auto& exports = script.GetExports();
        REQUIRE(exports.size() == 6);
        CHECK(exports[0].name == "speed");
        CHECK(std::get<double>(exports[0].value) == 3);
        CHECK(exports[1].name == "label");
        CHECK(std::get<std::string>(exports[1].value) == "crate");
        CHECK(std::get<Spot::Vector3>(exports[2].value) == Spot::Vector3{0, 1, 0});
        CHECK(std::get<Spot::Color>(exports[3].value) == Spot::Color{1, 1, 1, 1});
        CHECK(std::get<bool>(exports[4].value));
        CHECK(exports[5].name == "depth");
        CHECK(std::get<double>(exports[5].value) == -2);
        script.fieldValues["speed"] = 7.0;
        script.fieldValues["offset"] = Spot::Vector3{0, 5, 0};
        script.fieldValues["removed"] = 1.0;
        saved = scene.SaveToString();
    }
    CHECK(saved.find("\"removed\"") == std::string::npos);
    Spot::Scene scene;
    scene.Instantiate(saved);
    auto* script = scene.FindEntity("Crate")->GetComponent<Spot::ScriptComponent>();
    REQUIRE(script);
    script->fieldValues["active"] = false;
    scene.Update(Step);
    REQUIRE_FALSE(script->IsFaulted());
    CHECK(log.Messages() == std::vector<std::string>{"7 crate 5 1 false -2"});

    // Unfinished source keeps the fields read last.
    script->source = "speed = ";
    CHECK(script->GetExports().size() == 6);
    CHECK_THROWS(Spot::GetScriptExports("luau", "speed = ", "test.luau"));
}

TEST_CASE("Luau script profiling collects functions of running components", "[luau][tools][profile]") {
    Log log;
    Spot::Scene scene;
    for (const auto* name : {"First", "Second"}) {
        AddScript(scene.CreateEntity(name), R"(
            local spot = require("@spot")
            local function Advance(deltaTime: number)
                local items = {}
                for i = 1, 4 do
                    table.insert(items, { i })
                end
                spot.entity.transform.position.x += deltaTime * #items
            end
            function OnUpdate(deltaTime: number)
                Advance(deltaTime)
            end
        )");
    }
    Spot::ResetScriptProfile();
    scene.Update(Step);
    CHECK(Spot::GetScriptProfile().empty());
    Spot::SetScriptProfiling(true);
    for (int i = 0; i < 3; ++i)
        scene.Update(Step);
    Spot::SetScriptProfiling(false);
    const auto profile = Spot::GetScriptProfile();
    const auto advance = std::ranges::find(profile, "Advance", &Spot::ScriptFunctionProfile::name);
    REQUIRE(advance != profile.end());
    CHECK(advance->calls == 6);
    CHECK(advance->sourceName == "test.luau");
    CHECK(advance->line == 3);
    CHECK(advance->instructions > 0);
    CHECK(advance->allocations >= 6 * 5);
    const auto update = std::ranges::find(profile, "OnUpdate", &Spot::ScriptFunctionProfile::name);
    REQUIRE(update != profile.end());
    CHECK(update->calls == 6);
    scene.Update(Step);
    const auto unchanged = Spot::GetScriptProfile();
    CHECK(std::ranges::find(unchanged, "Advance", &Spot::ScriptFunctionProfile::name)->calls == 6);
    Spot::ResetScriptProfile();
    CHECK(Spot::GetScriptProfile().empty());
}

TEST_CASE("Luau components swap in new code while keeping their globals", "[luau][tools][reload]") {
    Log log;
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Counter");
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    const auto source = [](std::string text) {
        auto asset = std::make_shared<Spot::ScriptSource>();
        asset->source = std::move(text);
        asset->sourceName = "scripts/counter.luau";
        return asset;
    };
    script.script = source(R"(
        local spot = require("@spot")
        count = 0
        label = "first"
        items = { 1, 2 }
        target = nil
        local private = 0
        function OnStart() target = spot.entity end
        function OnUpdate(deltaTime) count += 1; private += 1 end
    )");
    for (int i = 0; i < 3; ++i)
        scene.Update(Step);
    REQUIRE_FALSE(script.IsFaulted());

    script.script = source(R"(
        local spot = require("@spot")
        count = 100
        label = 7
        items = {}
        target = nil
        extra = 5
        local private = 0
        function OnStart() print("not again") end
        function OnUpdate(deltaTime) count += 10 end
        function OnReload()
            print(`reloaded {count} {label} {#items} {target.name} {extra} {private}`)
        end
    )");
    scene.Update(Step);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(log.Messages() == std::vector<std::string>{"reloaded 3 7 0 Counter 5 0"});
    CHECK(std::get<double>(script.GetScriptObject()->Get("count")) == 13);
    CHECK(log.Contains("Reloaded 'counter'"));

    script.script = source("count = ");
    scene.Update(Step);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(log.Contains("Kept the running code of 'counter'"));
    CHECK(std::get<double>(script.GetScriptObject()->Get("count")) == 23);
}

TEST_CASE("Luau breakpoints report the call stack and let the frame continue", "[luau][tools][debug]") {
    Log log;
    Spot::Scene scene;
    auto& script = AddScript(scene.CreateEntity("Walker"), "steps = 0\n"
                                                           "local function Advance(by: number)\n"
                                                           "    local before = steps\n"
                                                           "    steps = before + by\n"
                                                           "end\n"
                                                           "function OnUpdate(deltaTime)\n"
                                                           "    -- walk\n"
                                                           "    local position = Vector3(1, 2, 3)\n"
                                                           "    Advance(1)\n"
                                                           "end\n");
    script.sourceName = "scripts/walker.luau";
    std::vector<Spot::ScriptBreak> stops;
    Spot::SetScriptBreakHandler([&](const Spot::ScriptBreak& stop) { stops.push_back(stop); });
    Spot::SetScriptBreakpoints("scripts/walker.luau", {4});
    scene.Update(Step);
    scene.Update(Step);
    REQUIRE(stops.size() == 2);
    REQUIRE(stops[1].frames.size() == 2);
    const auto& frame = stops[1].frames.front();
    CHECK(frame.function == "Advance");
    CHECK(frame.sourceName == "scripts/walker.luau");
    CHECK(frame.line == 4);
    CHECK(std::ranges::find(frame.locals, std::pair<std::string, std::string>{"before", "1"}) != frame.locals.end());
    CHECK(std::ranges::find(frame.locals, std::pair<std::string, std::string>{"by", "1"}) != frame.locals.end());
    const auto& caller = stops[1].frames[1];
    CHECK(caller.function == "OnUpdate");
    CHECK(caller.line == 9);
    CHECK(std::ranges::find(caller.locals, std::pair<std::string, std::string>{"position", "Vector3(1, 2, 3)"}) !=
          caller.locals.end());
    CHECK(std::get<double>(script.GetScriptObject()->Get("steps")) == 2);

    // A breakpoint on a line without code stops at the next line that has some.
    Spot::SetScriptBreakpoints("scripts/walker.luau", {7});
    scene.Update(Step);
    REQUIRE(stops.size() == 3);
    CHECK(stops[2].frames.front().line == 8);

    Spot::SetScriptBreakpoints("scripts/walker.luau", {});
    scene.Update(Step);
    CHECK(stops.size() == 3);
    Spot::SetScriptBreakHandler({});
    REQUIRE_FALSE(script.IsFaulted());
}
