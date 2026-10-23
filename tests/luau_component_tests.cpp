#include "assets/asset_manifest.h"
#include "assets/asset_pak.h"
#include "log_capture.h"
#include "script_asset_fixture.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

struct Console {
    std::shared_ptr<Spot::Tests::CaptureSink> sink = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks{{sink}};

    std::vector<std::string> Messages() const {
        std::vector<std::string> messages;
        for (const auto& record : sink->GetRecords()) {
            if (record.loggerName == "Luau") {
                CHECK(record.level == Spot::LogLevel::Info);
                messages.push_back(record.message);
            }
        }
        return messages;
    }
};

constexpr auto CounterSource = R"(
    local frames = 0
    function OnStart()
        print("start", nil)
    end
    function OnUpdate(dt)
        frames = frames + 1
        print(frames, dt)
    end
)";

} // namespace

TEST_CASE("Luau components print through engine logs and follow the component lifecycle", "[luau][scene]") {
    Console console;
    Spot::Scene scene;
    auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    component.language = "luau";
    component.source = CounterSource;
    component.SetEnabled(false);
    scene.Update(0.25f);
    CHECK(console.Messages().empty());
    component.SetEnabled(true);
    scene.Update(0.25f);
    scene.Update(0.5f);
    CHECK(console.Messages() == std::vector<std::string>{"start nil", "1 0.25", "2 0.5"});
    component.SetEnabled(false);
    scene.Update(0.25f);
    component.SetEnabled(true);
    scene.Update(0.25f);
    CHECK(console.Messages() == std::vector<std::string>{"start nil", "1 0.25", "2 0.5", "3 0.25"});
    component.GetEntity().SetActive(false);
    scene.Update(0.25f);
    CHECK(console.Messages().size() == 4);
    component.GetEntity().Destroy();
    scene.Update(0.25f);
    CHECK(console.Messages().size() == 4);
}

TEST_CASE("Luau components keep independent variables", "[luau][scene]") {
    Console console;
    Spot::Scene scene;
    {
        auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
        component.language = "luau";
        component.source = CounterSource;
    }
    {
        auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
        component.language = "luau";
        component.source = CounterSource;
    }
    scene.Update(0.25f);
    scene.Update(0.25f);
    const auto messages = console.Messages();
    REQUIRE(messages.size() == 6);
    CHECK(messages[2] == "1 0.25");
    CHECK(messages[3] == "1 0.25");
    CHECK(messages[4] == "2 0.25");
    CHECK(messages[5] == "2 0.25");
}

TEST_CASE("Luau source round trips through scenes and resets runtime state", "[luau][scene]") {
    Console console;
    Spot::Scene scene;
    auto& component = scene.CreateEntity("Scripted").AddComponent<Spot::ScriptComponent>();
    component.language = "luau";
    component.source = CounterSource;
    component.sourceName = "counter.luau";
    scene.Update(0.25f);
    const std::string saved = scene.SaveToString();
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(saved));
    const auto* restored = loaded.GetRootEntities()[0]->GetComponent<Spot::ScriptComponent>();
    REQUIRE(restored);
    CHECK(restored->language == "luau");
    CHECK(restored->source == component.source);
    CHECK(restored->sourceName == "counter.luau");
    loaded.Update(0.25f);
    CHECK(console.Messages() == std::vector<std::string>{"start nil", "1 0.25", "start nil", "1 0.25"});
}

TEST_CASE("Luau failures stop one component and are logged once with script location", "[luau][scene]") {
    Console console;
    Spot::Scene scene;
    auto& bad = scene.CreateEntity("Broken").AddComponent<Spot::ScriptComponent>();
    bad.language = "luau";
    bad.sourceName = "broken.luau";
    bad.source = "function OnUpdate(dt)\n    print(missing)\nend";
    auto& good = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    good.language = "luau";
    good.source = "function OnUpdate(dt) print(\"alive\") end";
    scene.Update(0.25f);
    scene.Update(0.25f);
    CHECK(bad.IsFaulted());
    CHECK_FALSE(good.IsFaulted());
    CHECK(console.Messages() == std::vector<std::string>{"alive", "alive"});
    std::size_t errors = 0;
    for (const auto& record : console.sink->GetRecords()) {
        if (record.level == Spot::LogLevel::Error) {
            ++errors;
            CHECK(record.message.find("broken.luau:2:") != std::string::npos);
            CHECK(record.message.find("Broken") != std::string::npos);
        }
    }
    CHECK(errors == 1);
}

TEST_CASE("Luau lifecycle signatures are checked before any script executes", "[luau][scene]") {
    for (const auto* source : {"function OnStart(arg) print(arg) end", "function OnUpdate() print(1) end",
                               "function OnStart() print(1) end\nfunction OnUpdate(a, b) end", "function OnStart()"}) {
        INFO(source);
        Console console;
        Spot::Scene scene;
        auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
        component.language = "luau";
        component.source = source;
        scene.Update(0.25f);
        CHECK(component.IsFaulted());
        CHECK(console.Messages().empty());
    }
}

TEST_CASE("Luau lifecycle functions are optional", "[luau][scene]") {
    Console console;
    Spot::Scene scene;
    auto& empty = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    empty.language = "luau";
    auto& startOnly = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    startOnly.language = "luau";
    startOnly.source = "function OnStart() print(\"started\") end";
    scene.Update(0.25f);
    scene.Update(0.25f);
    CHECK_FALSE(empty.IsFaulted());
    CHECK_FALSE(startOnly.IsFaulted());
    CHECK(console.Messages() == std::vector<std::string>{"started"});
}

TEST_CASE("Luau file assets are shared while component state stays independent", "[luau][assets]") {
    Console console;
    ScriptAssetFixture fixture;
    const auto id = fixture.Import("scripts/counter.luau", CounterSource);
    Spot::AssetManager assets(fixture.imported);
    Spot::Scene scene;
    for (int i = 0; i < 2; ++i) {
        auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
        component.script = assets.Load<Spot::ScriptSource>(id);
        REQUIRE(component.script);
        component.source = "function OnStart() print(\"embedded fallback\") end";
    }
    scene.Update(0.25f);
    scene.Update(0.25f);
    CHECK(console.Messages() ==
          std::vector<std::string>{"start nil", "start nil", "1 0.25", "1 0.25", "2 0.25", "2 0.25"});
    const auto saved = scene.SaveToString();
    CHECK(saved.find(id.ToString()) != std::string::npos);
    CHECK(saved.find("local frames") == std::string::npos);
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(saved, &assets));
    const auto* component = loaded.GetRootEntities()[0]->GetComponent<Spot::ScriptComponent>();
    REQUIRE(component);
    REQUIRE(component->script);
    CHECK(component->script.GetId() == id);
    CHECK(component->script.Get() == assets.Load<Spot::ScriptSource>(id).Get());
}

TEST_CASE("Luau scenes execute file assets from a shipped pak", "[luau][assets]") {
    Console console;
    ScriptAssetFixture fixture;
    const auto id = fixture.Import("scripts/shipped.luau", "function OnStart() print(\"shipped\", nil) end");
    {
        Spot::AssetManager assets(fixture.imported);
        Spot::Scene scene;
        {
            auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
            component.script = assets.Load<Spot::ScriptSource>(id);
        }
        fixture.Write("level.scene", scene.SaveToString());
    }
    REQUIRE(fixture.database.Refresh() == 1);
    const auto shipped = fixture.temp.GetPath() / "shipped";
    std::filesystem::create_directories(shipped);
    std::filesystem::copy_file(Spot::Detail::Manifest::GetPath(fixture.imported),
                               Spot::Detail::Manifest::GetPath(shipped));
    REQUIRE(Spot::Detail::WriteAssetPak(shipped / Spot::Detail::AssetPakFileName, fixture.imported));
    std::filesystem::rename(fixture.sources, fixture.temp.GetPath() / "unavailable_sources");
    Spot::AssetManager assets(shipped);
    auto scene = Spot::InstantiateScene(assets, "level.scene");
    REQUIRE(scene);
    scene->Update(0.25f);
    CHECK(console.Messages() == std::vector<std::string>{"shipped nil"});
}

TEST_CASE("Luau asset errors use the relative file path in diagnostics", "[luau][assets]") {
    Console console;
    ScriptAssetFixture fixture;
    const auto id = fixture.Import("scripts/broken.luau", "function OnUpdate(dt)\n    print(missing)\nend");
    Spot::AssetManager assets(fixture.imported);
    Spot::Scene scene;
    auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    component.script = assets.Load<Spot::ScriptSource>(id);
    component.sourceName = "unused embedded name";
    scene.Update(0.25f);
    REQUIRE(component.IsFaulted());
    REQUIRE_FALSE(console.sink->GetRecords().empty());
    CHECK(console.sink->GetRecords().back().message.find("scripts/broken.luau:2:") != std::string::npos);
}

TEST_CASE("Luau source modules load imported assets and share the explicit spot context", "[luau][assets][libraries]") {
    Console console;
    ScriptAssetFixture fixture;
    fixture.Import("scripts/combat.luau", R"(
        local spot = require("spot")
        local M = { calls = 0 }
        function M.advance(amount)
            M.calls = M.calls + 1
            spot.entity.Transform.position.x = spot.entity.Transform.position.x + amount
        end
        return M
    )");
    const auto root = fixture.Import("scripts/player.luau", R"(
        local combat = require("combat")
        local Battle = require("./combat.luau")
        function OnStart()
            combat.advance(3)
        end
        function OnUpdate(delta_time)
            if combat ~= Battle then missing() end
            Battle.advance(delta_time)
            print(combat.calls)
        end
    )");
    auto directory = fixture.imported;
    SECTION("Imported files") {
    }
    SECTION("Pak without sources or loose imported files") {
        directory = fixture.temp.GetPath() / "shipped";
        std::filesystem::create_directories(directory);
        std::filesystem::copy_file(Spot::Detail::Manifest::GetPath(fixture.imported),
                                   Spot::Detail::Manifest::GetPath(directory));
        REQUIRE(Spot::Detail::WriteAssetPak(directory / Spot::Detail::AssetPakFileName, fixture.imported));
        std::filesystem::rename(fixture.sources, fixture.temp.GetPath() / "unavailable_sources");
    }
    Spot::AssetManager assets(directory);
    struct AssetScope {
        Spot::AssetManager* previous = Spot::Assets::GetManager();
        ~AssetScope() { Spot::Detail::SetAssetManager(previous); }
    } scope;
    Spot::Detail::SetAssetManager(&assets);
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Player");
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.script = assets.Load<Spot::ScriptSource>(root);
    scene.Update(0.25f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(entity.GetTransform().GetPosition().x == 3.25f);
    scene.Update(0.25f);
    CHECK(entity.GetTransform().GetPosition().x == 3.5f);
    CHECK(console.Messages() == std::vector<std::string>{"2", "3"});
}

TEST_CASE("Luau engine modules require explicit require and accept local aliases", "[luau][scene][libraries]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "luau";
    SECTION("Local alias") {
        script.source =
            "local engine = require(\"spot\")\nfunction OnStart() engine.entity.Transform.position.x = 7 end";
        scene.Update(0.1f);
        CHECK_FALSE(script.IsFaulted());
        CHECK(entity.GetTransform().GetPosition().x == 7);
    }
    SECTION("No implicit module global") {
        script.source = "function OnStart() print(spot) end";
        scene.Update(0.1f);
        CHECK(script.IsFaulted());
    }
    SECTION("No implicit engine globals after require") {
        script.source = "local spot = require(\"spot\")\nfunction OnStart() print(entity) end";
        scene.Update(0.1f);
        CHECK(script.IsFaulted());
    }
    SECTION("Sandboxed OS and IO libraries are unavailable") {
        script.source = "function OnStart() print(os) end";
        scene.Update(0.1f);
        CHECK(script.IsFaulted());
    }
}

TEST_CASE("Luau receives registered native engine libraries without adapter specific bindings", "[luau][libraries]") {
    class Combat final : public Spot::ScriptObject {
    public:
        Spot::ScriptValue Call(std::string_view name, std::span<const Spot::ScriptValue> arguments) override {
            if (name == "damage" && arguments.size() == 2)
                return std::get<double>(arguments[0]) - std::get<double>(arguments[1]);
            return ScriptObject::Call(name, arguments);
        }
    };
    Spot::RegisterScriptLibrary("combat", [] { return std::make_shared<Combat>(); });
    struct Cleanup {
        ~Cleanup() { Spot::UnregisterScriptLibrary("combat"); }
    } cleanup;
    Console console;
    Spot::Scene scene;
    auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    component.language = "luau";
    component.source = "local combat = require(\"combat\")\nfunction OnStart() print(combat.damage(10, 3)) end";
    scene.Update(0.1f);
    CHECK_FALSE(component.IsFaulted());
    CHECK(console.Messages() == std::vector<std::string>{"7"});
}

TEST_CASE("Luau supports file scope modules struct formatting and execution budgets", "[luau][scene][libraries]") {
    Console console;
    ScriptAssetFixture fixture;
    fixture.Import("scripts/math_helpers.luau", R"(
        local spot = require("spot")
        calls = 0
        function step(x)
            calls = calls + 1
            spot.entity.Transform.position = Vector3(x, 2, 3)
        end
    )");
    const auto root = fixture.Import("scripts/actor.luau", R"(
        local helpers = require("math_helpers")
        function OnStart()
            helpers.step(5)
            if Vector2(1, 2) ~= Vector2(1, 2) then missing() end
            print(Vector2(1, 2), Color(1, 0.5, 0.25, 1), helpers.calls)
        end
    )");
    Spot::AssetManager assets(fixture.imported);
    struct AssetScope {
        Spot::AssetManager* previous = Spot::Assets::GetManager();
        ~AssetScope() { Spot::Detail::SetAssetManager(previous); }
    } scope;
    Spot::Detail::SetAssetManager(&assets);

    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Actor");
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.script = assets.Load<Spot::ScriptSource>(root);
    scene.Update(0.1f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(entity.GetTransform().GetPosition() == Spot::Vector3{5, 2, 3});
    CHECK(console.Messages() == std::vector<std::string>{"Vector2(1, 2) Color(1, 0.5, 0.25, 1) 1"});

    auto& runaway = scene.CreateEntity("Runaway").AddComponent<Spot::ScriptComponent>();
    runaway.language = "luau";
    runaway.source = "function OnStart() while true do end end";
    scene.Update(0.1f);
    CHECK(runaway.IsFaulted());
}

TEST_CASE("Luau native lifecycle hooks support nested activity changes and removal cleanup", "[luau][scene]") {
    Console console;
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Actor");
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "luau";
    script.source = R"(
        local spot = require("spot")
        function OnEnable() print("enable") end
        function OnDisable() print("disable", spot.Component:IsEnabled()) end
        function OnUpdate(dt)
            spot.Component:SetEnabled(false)
            print("after", dt)
        end
        function OnDestroy() print("destroy", spot.Component:GetEntity():GetName(), spot.Component:IsValid()) end
    )";
    scene.Update(0);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(console.Messages() == std::vector<std::string>{"enable", "disable false", "after 0"});
    entity.RemoveComponent(script);
    scene.Update(0);
    CHECK(console.Messages() == std::vector<std::string>{"enable", "disable false", "after 0", "destroy Actor false"});
}

TEST_CASE("Luau bounds recursive native activity hooks", "[luau][scene]") {
    Console console;
    Spot::Scene scene;
    auto& script = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    script.language = "luau";
    script.source = R"(
        local spot = require("spot")
        function OnEnable() spot.Component:SetEnabled(false) end
        function OnDisable() spot.Component:SetEnabled(true) end
    )";
    scene.Update(0);
    CHECK(script.IsFaulted());
    auto& healthy = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    healthy.language = "luau";
    healthy.source = "function OnStart() print(42) end";
    scene.Update(0);
    CHECK_FALSE(healthy.IsFaulted());
    CHECK(console.Messages() == std::vector<std::string>{"42"});
}

TEST_CASE("Luau components run Luau syntax with type annotations and compound assignment", "[luau][scene]") {
    Console console;
    Spot::Scene scene;
    auto& component = scene.CreateEntity("Typed").AddComponent<Spot::ScriptComponent>();
    component.language = "luau";
    component.source = R"(
        local spot = require("spot")
        type Counter = { value: number }
        local counter: Counter = { value = 0 }
        local function add(amount: number): number
            counter.value += amount
            return counter.value
        end
        function OnUpdate(deltaTime: number)
            for i = 1, 3 do
                if i == 2 then continue end
                add(i)
            end
            print(`{spot.entity.name} {counter.value} {math.floor(deltaTime * 10)}`)
        end
    )";
    scene.Update(0.25f);
    REQUIRE_FALSE(component.IsFaulted());
    CHECK(console.Messages() == std::vector<std::string>{"Typed 4 2"});
}

TEST_CASE("Luau built-ins are read only and cannot be shadowed by script globals", "[luau][scene]") {
    for (const auto* source : {"math = {}", "Vector3 = 1", "print = nil", "math.floor = nil",
                               "string.upper = function() end", "function OnStart() local x = debug end"}) {
        INFO(source);
        Console console;
        Spot::Scene scene;
        auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
        component.language = "luau";
        component.source = source;
        scene.Update(0.1f);
        CHECK(component.IsFaulted());
    }
    Console console;
    Spot::Scene scene;
    auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    component.language = "luau";
    component.source = "local floor = math.floor\nfunction OnStart() print(floor(2.5), bit32.band(6, 3)) end";
    scene.Update(0.1f);
    CHECK_FALSE(component.IsFaulted());
    CHECK(console.Messages() == std::vector<std::string>{"2 2"});
}
